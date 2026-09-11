#include "PoseEstimatorHandle.h"

#include "DetectorHandle.h"

namespace photonvision::vkapriltag_jni {

namespace {
// Layout of one tag's data in VkAprilTagJNI.detect()'s wire format, starting
// at that tag's own base offset (i.e. NOT counting the leading tag-count
// double at index 0 of the full buffer - see VkAprilTagJNI.cpp's detect()
// wrapper for where that's stripped before calling EstimatePoses).
constexpr int kDoublesPerDetection = 22;
constexpr int kCornersOffset = 5;      // 8 doubles: x0,y0,x1,y1,x2,y2,x3,y3
constexpr int kHomographyOffset = 13;  // 9 doubles: row-major 3x3

constexpr int kDoublesPerPose = 15;  // R[9] + t[3] + error + valid + iterations

void WritePose(const apriltag_vulkan::TagPose &pose, std::vector<double> &out) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) out.push_back(pose.R[r][c]);
  }
  out.push_back(pose.t[0]);
  out.push_back(pose.t[1]);
  out.push_back(pose.t[2]);
  out.push_back(pose.error);
  out.push_back(pose.valid ? 1.0 : 0.0);
  out.push_back(static_cast<double>(pose.iterations));
}

void WriteInvalidPose(std::vector<double> &out) {
  for (int i = 0; i < kDoublesPerPose; ++i) out.push_back(0.0);
}
}  // namespace

std::unique_ptr<PoseEstimatorHandle> CreatePoseEstimator(double fx, double fy, double cx,
                                                         double cy, double tagsize,
                                                         uint32_t cpu_threads) {
  ClearLastError();
  try {
    auto handle = std::make_unique<PoseEstimatorHandle>();
    handle->estimator = std::make_unique<apriltag_vulkan::PoseEstimator>(
        apriltag_vulkan::CameraIntrinsics{fx, fy, cx, cy}, tagsize, cpu_threads);
    return handle;
  } catch (const std::exception &e) {
    SetLastError(e.what());
    return nullptr;
  } catch (...) {
    SetLastError("unknown native exception in CreatePoseEstimator");
    return nullptr;
  }
}

std::vector<double> EstimatePoses(PoseEstimatorHandle &handle, const double *detections_flat,
                                  int count, bool *ok) {
  ClearLastError();
  *ok = false;

  if (count < 0) {
    SetLastError("EstimatePoses called with a negative count");
    return {};
  }

  std::vector<double> out;
  out.reserve(static_cast<size_t>(count) * 2 * kDoublesPerPose);

  try {
    for (int i = 0; i < count; ++i) {
      const double *base = detections_flat + static_cast<size_t>(i) * kDoublesPerDetection;

      double corners[4][2];
      for (int c = 0; c < 4; ++c) {
        corners[c][0] = base[kCornersOffset + c * 2];
        corners[c][1] = base[kCornersOffset + c * 2 + 1];
      }
      double H[3][3];
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) H[r][c] = base[kHomographyOffset + r * 3 + c];
      }

      apriltag_vulkan::TagPosePair pair = handle.estimator->EstimateBoth(corners, H);
      if (pair.solution1.valid && pair.solution2.valid) {
        // Lower-error solution first, matching Estimate()'s own ranking rule.
        if (pair.solution2.error < pair.solution1.error) {
          WritePose(pair.solution2, out);
          WritePose(pair.solution1, out);
        } else {
          WritePose(pair.solution1, out);
          WritePose(pair.solution2, out);
        }
      } else if (pair.solution1.valid) {
        WritePose(pair.solution1, out);
        WriteInvalidPose(out);
      } else if (pair.solution2.valid) {
        WritePose(pair.solution2, out);
        WriteInvalidPose(out);
      } else {
        WriteInvalidPose(out);
        WriteInvalidPose(out);
      }
    }
    *ok = true;
    return out;
  } catch (const std::exception &e) {
    SetLastError(e.what());
    return {};
  } catch (...) {
    SetLastError("unknown native exception in EstimatePoses");
    return {};
  }
}

}  // namespace photonvision::vkapriltag_jni
