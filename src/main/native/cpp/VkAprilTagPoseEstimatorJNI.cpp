// JNI entry points for org.photonvision.vkapriltag.VkAprilTagPoseEstimatorJNI. Same
// exception-safety/symbol-visibility rules as VkAprilTagJNI.cpp - see that file's header comment.

#include <jni.h>

#include <string>
#include <vector>

#include "DetectorHandle.h"
#include "PoseEstimatorHandle.h"
#include "org_photonvision_vkapriltag_VkAprilTagPoseEstimatorJNI.h"

using photonvision::vkapriltag_jni::CreatePoseEstimator;
using photonvision::vkapriltag_jni::EstimatePoses;
using photonvision::vkapriltag_jni::LastError;
using photonvision::vkapriltag_jni::PoseEstimatorHandle;
using photonvision::vkapriltag_jni::SetLastError;

namespace {

// Wire layout produced by VkAprilTagJNI.detect(): a leading tag count,
// followed by 22 doubles per tag. Kept in sync with DetectorHandle.cpp's
// Detect() and PoseEstimatorHandle.cpp's kDoublesPerDetection.
constexpr int kDoublesPerDetection = 22;

PoseEstimatorHandle *AsHandle(jlong handle) {
  return reinterpret_cast<PoseEstimatorHandle *>(handle);
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_org_photonvision_vkapriltag_VkAprilTagPoseEstimatorJNI_create(
    JNIEnv *, jclass, jdouble fx, jdouble fy, jdouble cx, jdouble cy, jdouble tagsize,
    jint cpu_threads) {
  if (tagsize <= 0.0) {
    SetLastError("tagsize must be positive");
    return 0;
  }

  std::unique_ptr<PoseEstimatorHandle> handle =
      CreatePoseEstimator(fx, fy, cx, cy, tagsize, static_cast<uint32_t>(cpu_threads));
  if (handle == nullptr) return 0;

  return reinterpret_cast<jlong>(handle.release());
}

JNIEXPORT jdoubleArray JNICALL
Java_org_photonvision_vkapriltag_VkAprilTagPoseEstimatorJNI_estimatePoses(
    JNIEnv *env, jclass, jlong handle_ptr, jdoubleArray detections_flat_buffer) {
  PoseEstimatorHandle *handle = AsHandle(handle_ptr);
  if (handle == nullptr) {
    SetLastError("estimatePoses() called with a null handle");
    return nullptr;
  }
  if (detections_flat_buffer == nullptr) {
    SetLastError("estimatePoses() called with a null detections buffer");
    return nullptr;
  }

  const jsize len = env->GetArrayLength(detections_flat_buffer);
  if (len < 1) {
    SetLastError("detections buffer must have at least a leading tag count");
    return nullptr;
  }

  jdouble *elems = env->GetDoubleArrayElements(detections_flat_buffer, nullptr);
  if (elems == nullptr) return nullptr;  // OutOfMemoryError already thrown

  const int count = static_cast<int>(elems[0]);
  jdoubleArray result = nullptr;
  if (count < 0 ||
      static_cast<jsize>(1 + static_cast<int64_t>(count) * kDoublesPerDetection) != len) {
    SetLastError("detections buffer length does not match its own leading tag count");
  } else {
    bool ok = false;
    std::vector<double> flat = EstimatePoses(*handle, elems + 1, count, &ok);
    if (ok) {
      result = env->NewDoubleArray(static_cast<jsize>(flat.size()));
      if (result != nullptr) {
        env->SetDoubleArrayRegion(result, 0, static_cast<jsize>(flat.size()), flat.data());
      }
    }
  }

  env->ReleaseDoubleArrayElements(detections_flat_buffer, elems, JNI_ABORT);
  return result;
}

JNIEXPORT jstring JNICALL
Java_org_photonvision_vkapriltag_VkAprilTagPoseEstimatorJNI_getLastError(JNIEnv *env, jclass) {
  const std::string &message = LastError();
  if (message.empty()) return nullptr;
  return env->NewStringUTF(message.c_str());
}

JNIEXPORT void JNICALL Java_org_photonvision_vkapriltag_VkAprilTagPoseEstimatorJNI_destroy(
    JNIEnv *, jclass, jlong handle_ptr) {
  delete AsHandle(handle_ptr);
}

}  // extern "C"
