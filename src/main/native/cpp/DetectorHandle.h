#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "vkapriltag/TagDecoder.h"
#include "vkapriltag/apriltag_family.h"
#include "vkapriltag/gpu/GpuDetector.h"
#include "vkapriltag/gpu/QuadDecode.h"
#include "vkapriltag/vk/Context.h"

extern "C" {
#include "apriltag.h"
}

namespace photonvision::vkapriltag_jni {

// Everything create() builds and detect()/destroy() need back. One instance
// per JNI-side native handle, one native handle per camera - GpuDetector's
// per-frame mutable state (last_selected_extents, last_line_fit_points, the
// Context's command ring) is not safe to share across concurrent detect()
// calls.
struct DetectorHandle {
  std::unique_ptr<apriltag_vulkan::vk::Context> ctx;
  std::unique_ptr<apriltag_vulkan::GpuDetector> detector;
  std::unique_ptr<apriltag_vulkan::QuadDecode> quad_decode;
  apriltag_family_t *family = nullptr;  // owned; setup_tag_family()/teardown_tag_family()
  std::string family_name;              // needed by teardown_tag_family(), which wants it back
  apriltag_detector_t *td = nullptr;    // owned; apriltag_detector_create() + add_family
  std::unique_ptr<apriltag_vulkan::TagDecoder> tag_decoder;
  bool reversed_border = false;
  uint32_t width = 0;
  uint32_t height = 0;

  // detect() is not reentrant - GpuDetector/QuadDecode/TagDecoder all carry
  // state across their own single-frame call.
  std::mutex mtx;

  ~DetectorHandle();
};

// Last error from create()/detect() on the calling thread, or empty if the
// last call on this thread succeeded. Cleared at the start of every create()/
// detect() call.
const std::string &LastError();
void SetLastError(const std::string &message);
void ClearLastError();

// Runs setup_tag_family() + apriltag_detector_create() + the GpuDetector/
// QuadDecode/TagDecoder construction described in apps/apriltag_vulkan/
// main.cpp. Returns nullptr (and sets the thread-local last error) on any
// failure - never throws across the call site, since this is the last C++
// frame before the JNI boundary.
std::unique_ptr<DetectorHandle> CreateDetector(uint32_t width, uint32_t height,
                                               const std::string &family_name,
                                               uint32_t cpu_threads, int32_t device_index);

// Runs the full pipeline (GpuDetector::Detect -> QuadDecode::Decode ->
// TagDecoder::Decode) on one CV_8UC1, continuous, width x height grayscale
// frame and flattens the resulting zarray_t<apriltag_detection_t*> into the
// wire format documented on VkAprilTagJNI.detect(): a leading tag count
// followed by 22 doubles per tag (id, hamming, decision margin, center x/y,
// 4 corners, row-major 3x3 homography). Returns an empty vector (not
// nullopt) with the last error set on failure, since an empty result and "no
// tags found" must be distinguishable to the JNI wrapper by return value
// alone (see VkAprilTagJNI.cpp).
std::vector<double> Detect(DetectorHandle &handle, const uint8_t *gray, uint32_t width,
                           uint32_t height, bool *ok);

}  // namespace photonvision::vkapriltag_jni
