#include "DetectorHandle.h"

#include <cstring>

namespace photonvision::vkapriltag_jni {

namespace {
thread_local std::string g_last_error;
}  // namespace

const std::string &LastError() { return g_last_error; }
void SetLastError(const std::string &message) { g_last_error = message; }
void ClearLastError() { g_last_error.clear(); }

DetectorHandle::~DetectorHandle() {
  // Destruction order matters: tag_decoder/quad_decode/detector hold no
  // reference to td/family, but td must outlive tag_decoder (TagDecoder
  // does not own td - see TagDecoder.h) and ctx must outlive detector
  // (GpuDetector holds a reference, not a copy). unique_ptr member
  // destruction already runs in reverse declaration order, which is the
  // order below; td/family are destroyed after tag_decoder for the same
  // reason, and before ctx is not required but kept for symmetry with the
  // sample app's teardown in apps/apriltag_vulkan/main.cpp.
  tag_decoder.reset();
  if (td != nullptr) {
    apriltag_detector_destroy(td);
    td = nullptr;
  }
  if (family != nullptr) {
    teardown_tag_family(&family, family_name.c_str());
  }
  quad_decode.reset();
  detector.reset();
  ctx.reset();
}

std::unique_ptr<DetectorHandle> CreateDetector(uint32_t width, uint32_t height,
                                               const std::string &family_name,
                                               uint32_t cpu_threads, int32_t device_index) {
  ClearLastError();

  if (width % 8 != 0 || height % 8 != 0) {
    SetLastError("width and height must both be multiples of 8 (got " + std::to_string(width) +
                "x" + std::to_string(height) + ")");
    return nullptr;
  }

  auto handle = std::make_unique<DetectorHandle>();
  handle->width = width;
  handle->height = height;
  handle->family_name = family_name;

  try {
    if (!setup_tag_family(&handle->family, family_name.c_str())) {
      SetLastError("unknown tag family: " + family_name);
      return nullptr;
    }

    handle->td = apriltag_detector_create();
    apriltag_detector_add_family(handle->td, handle->family);
    // RefineEdges (camera-distortion based edge refinement) is out of scope
    // for this detector - see TagDecoder.h - so make sure the fetched
    // apriltag library doesn't try to run it against quads it never
    // produced itself.
    handle->td->refine_edges = false;

    handle->reversed_border = handle->family->reversed_border;

    apriltag_vulkan::vk::ContextOptions ctx_options;
    ctx_options.device_index = device_index;
    // Deliberately left off: a silently-selected software Vulkan
    // implementation (Mesa lavapipe/llvmpipe) is functionally perfect and
    // ~100x slower, which is indistinguishable from "the GPU port is slow"
    // unless something says so out loud - see vk/Context.h's comment on
    // this same field.
    ctx_options.allow_cpu_device = false;
    // The library's default prints the selected device and launch geometry
    // to stderr on every construction; that's noise once this runs inside
    // a long-lived PhotonVision process instead of a one-shot CLI tool.
    ctx_options.verbose = false;

    handle->ctx = std::make_unique<apriltag_vulkan::vk::Context>(ctx_options);

    apriltag_vulkan::DetectorConfig config;
    config.width = width;
    config.height = height;
    config.tag_width = static_cast<uint32_t>(handle->family->width_at_border);
    config.reversed_border = handle->family->reversed_border;
    config.normal_border = !handle->family->reversed_border;
    config.cpu_threads = cpu_threads;

    handle->detector = std::make_unique<apriltag_vulkan::GpuDetector>(*handle->ctx, config);
    handle->quad_decode = std::make_unique<apriltag_vulkan::QuadDecode>(config);
    handle->tag_decoder = std::make_unique<apriltag_vulkan::TagDecoder>(handle->td);

    return handle;
  } catch (const std::exception &e) {
    SetLastError(e.what());
    return nullptr;
  } catch (...) {
    SetLastError("unknown native exception in CreateDetector");
    return nullptr;
  }
}

std::vector<double> Detect(DetectorHandle &handle, const uint8_t *gray, uint32_t width,
                           uint32_t height, bool *ok) {
  ClearLastError();
  *ok = false;

  if (width != handle.width || height != handle.height) {
    SetLastError("frame size " + std::to_string(width) + "x" + std::to_string(height) +
                " does not match the detector's configured " + std::to_string(handle.width) +
                "x" + std::to_string(handle.height));
    return {};
  }

  std::lock_guard<std::mutex> lock(handle.mtx);

  try {
    handle.detector->Detect(gray);
    std::vector<apriltag_vulkan::DetectedQuad> quads = handle.quad_decode->Decode(
        handle.detector->last_selected_extents, handle.detector->last_line_fit_points);
    zarray_t *detections =
        handle.tag_decoder->Decode(quads, gray, width, height, handle.reversed_border);

    const int n = zarray_size(detections);
    std::vector<double> out;
    out.reserve(1 + static_cast<size_t>(n) * 22);
    out.push_back(static_cast<double>(n));

    for (int i = 0; i < n; ++i) {
      apriltag_detection_t *det = nullptr;
      zarray_get(detections, i, &det);

      out.push_back(static_cast<double>(det->id));
      out.push_back(static_cast<double>(det->hamming));
      out.push_back(static_cast<double>(det->decision_margin));
      out.push_back(det->c[0]);
      out.push_back(det->c[1]);
      for (int c = 0; c < 4; ++c) {
        out.push_back(det->p[c][0]);
        out.push_back(det->p[c][1]);
      }
      // matd_t is row-major, ncols == nrows == 3 for a homography - see
      // common/matd.h's MATD_EL macro.
      if (det->H != nullptr && det->H->nrows == 3 && det->H->ncols == 3) {
        for (int idx = 0; idx < 9; ++idx) out.push_back(det->H->data[idx]);
      } else {
        for (int idx = 0; idx < 9; ++idx) out.push_back(idx % 4 == 0 ? 1.0 : 0.0);  // identity
      }
    }

    *ok = true;
    return out;
  } catch (const std::exception &e) {
    SetLastError(e.what());
    return {};
  } catch (...) {
    SetLastError("unknown native exception in Detect");
    return {};
  }
}

}  // namespace photonvision::vkapriltag_jni
