#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vkapriltag/PoseEstimator.h"

namespace photonvision::vkapriltag_jni {

// Owns one apriltag_vulkan::PoseEstimator, configured for one fixed (camera
// intrinsics, tag size) pair - independent of any DetectorHandle, since
// PoseEstimator needs neither a detector, a tag family, nor a device. A
// caller with several tag sizes or several cameras needs one
// PoseEstimatorHandle per (intrinsics, tag size) combination, exactly as it
// already needs one DetectorHandle per (family, width, height, device,
// decimation) combination.
struct PoseEstimatorHandle {
  std::unique_ptr<apriltag_vulkan::PoseEstimator> estimator;
};

// Constructs a PoseEstimator for the given intrinsics/tag size/thread count.
// Returns nullptr (and sets the thread-local last error) on failure - never
// throws across the call site.
std::unique_ptr<PoseEstimatorHandle> CreatePoseEstimator(double fx, double fy, double cx,
                                                         double cy, double tagsize,
                                                         uint32_t cpu_threads);

// Runs PoseEstimator::EstimateBoth for every tag packed into
// `detections_flat`, which must be in VkAprilTagJNI.detect()'s existing
// 22-doubles-per-tag wire format (only each tag's 4 corners, at offsets
// base+5..base+12, and row-major 3x3 homography, at offsets
// base+13..base+21, are read - id/hamming/margin/center are ignored here).
// Writes 30 doubles per tag to the returned buffer: two back-to-back
// 15-double TagPoses (R[9] row-major, t[3], error, valid as 0.0/1.0,
// iterations), lower-error solution first, matching TagPosePair's
// solution1/solution2 order before ranking - solution1 is EstimateBoth's
// homography-seeded orthogonal-iteration result, solution2 the second
// ambiguity candidate if one exists. A tag whose own pose could not be
// computed has valid=0.0 (and the rest of its 15 doubles zeroed) without
// aborting the rest of the batch. Returns an empty vector (not one sized for
// `count`) with the last error set on total failure (e.g. a malformed input
// buffer whose length doesn't match `count`).
std::vector<double> EstimatePoses(PoseEstimatorHandle &handle, const double *detections_flat,
                                  int count, bool *ok);

}  // namespace photonvision::vkapriltag_jni
