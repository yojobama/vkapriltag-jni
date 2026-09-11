// JNI entry points for org.photonvision.vkapriltag.VkAprilTagJNI. Each of
// these is the very last C++ frame before crossing into the JVM, so none of
// them may let a C++ exception escape - DetectorHandle.h's CreateDetector/
// Detect already catch everything and report failure through a return value
// plus a thread-local last-error string; the wrappers here just translate
// that into the Java-visible shapes (0/null on failure, jobjectArray,
// jdoubleArray, ...).
//
// Every function below carries JNIEXPORT, which jni.h defines as default
// visibility on ELF/Mach-O and __declspec(dllexport) on Windows - the actual
// exported symbol surface either way. On Linux this is additionally locked
// down by exports.map (see CMakeLists.txt): everything except these
// Java_org_photonvision_vkapriltag_* entry points and JNI_OnLoad is hidden,
// so this shared object cannot leak libapriltag/zarray/matd symbols into a
// process that has also loaded WPILib's own (unpatched, differently built)
// apriltag native library - see the plan's blocker writeup for why that
// collision matters.

#include <jni.h>

#include <cstring>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "DetectorHandle.h"
#include "org_photonvision_vkapriltag_VkAprilTagJNI.h"
#include "vkapriltag/vk/Context.h"

using photonvision::vkapriltag_jni::CreateDetector;
using photonvision::vkapriltag_jni::Detect;
using photonvision::vkapriltag_jni::DetectorHandle;
using photonvision::vkapriltag_jni::LastError;
using photonvision::vkapriltag_jni::SetLastError;
using photonvision::vkapriltag_jni::ValidateGeometry;

namespace {

// Reinterprets a jlong handle back into a DetectorHandle*. jlong is
// guaranteed >= 64 bits by the JNI spec, so this round-trips exactly on every
// platform PhotonVision targets.
DetectorHandle *AsHandle(jlong handle) { return reinterpret_cast<DetectorHandle *>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL Java_org_photonvision_vkapriltag_VkAprilTagJNI_isSupported(JNIEnv *,
                                                                                     jclass) {
  try {
    for (const auto &caps : apriltag_vulkan::vk::Context::EnumerateDevices()) {
      if (!caps.is_cpu_device()) return JNI_TRUE;
    }
  } catch (...) {
    // A broken/missing Vulkan loader is exactly what this probe exists to
    // detect - report "unsupported", not a Java exception.
  }
  return JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL Java_org_photonvision_vkapriltag_VkAprilTagJNI_enumerateDevices(
    JNIEnv *env, jclass) {
  std::vector<apriltag_vulkan::vk::DeviceCaps> devices;
  try {
    devices = apriltag_vulkan::vk::Context::EnumerateDevices();
  } catch (const std::exception &e) {
    SetLastError(e.what());
  } catch (...) {
    SetLastError("unknown native exception in EnumerateDevices");
  }

  jclass info_class = env->FindClass("org/photonvision/vkapriltag/VkAprilTagDeviceInfo");
  if (info_class == nullptr) return nullptr;  // FindClass already threw

  jmethodID ctor = env->GetMethodID(info_class, "<init>",
                                    "(ILjava/lang/String;Ljava/lang/String;ZI)V");
  if (ctor == nullptr) return nullptr;

  jobjectArray result =
      env->NewObjectArray(static_cast<jsize>(devices.size()), info_class, nullptr);
  if (result == nullptr) return nullptr;

  for (size_t i = 0; i < devices.size(); ++i) {
    const auto &caps = devices[i];
    // Context::DescribeDevice() is a multi-line stderr diagnostic (device
    // line, then a "limits:" line, then a "chosen geometry:" line - see
    // Context.cpp) prefixed with "apriltag_vulkan: using ", not a one-line
    // UI label. Trim it down to just the "<name> (<type>, Vulkan x.y.z)"
    // clause a device picker actually wants.
    std::string description = apriltag_vulkan::vk::Context::DescribeDevice(caps);
    const size_t newline = description.find('\n');
    if (newline != std::string::npos) description.erase(newline);
    const std::string prefix = "apriltag_vulkan: using ";
    if (description.rfind(prefix, 0) == 0) description.erase(0, prefix.size());

    jstring name = env->NewStringUTF(caps.name.c_str());
    jstring desc = env->NewStringUTF(description.c_str());
    jobject entry = env->NewObject(info_class, ctor, static_cast<jint>(i), name, desc,
                                   static_cast<jboolean>(caps.is_cpu_device() ? JNI_TRUE
                                                                              : JNI_FALSE),
                                   static_cast<jint>(caps.api_version));
    env->SetObjectArrayElement(result, static_cast<jsize>(i), entry);

    env->DeleteLocalRef(name);
    env->DeleteLocalRef(desc);
    if (entry != nullptr) env->DeleteLocalRef(entry);
    if (env->ExceptionCheck()) return nullptr;
  }

  return result;
}

JNIEXPORT jlong JNICALL Java_org_photonvision_vkapriltag_VkAprilTagJNI_create(
    JNIEnv *env, jclass, jint width, jint height, jint decimation, jstring family,
    jint cpu_threads, jint device_index) {
  if (width <= 0 || height <= 0) {
    SetLastError("width and height must be positive");
    return 0;
  }
  if (decimation <= 0) {
    SetLastError("decimation must be positive");
    return 0;
  }

  const char *family_chars = env->GetStringUTFChars(family, nullptr);
  if (family_chars == nullptr) return 0;  // OutOfMemoryError already thrown
  const std::string family_name(family_chars);
  env->ReleaseStringUTFChars(family, family_chars);

  std::unique_ptr<DetectorHandle> handle =
      CreateDetector(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                    static_cast<uint32_t>(decimation), family_name,
                    static_cast<uint32_t>(cpu_threads), static_cast<int32_t>(device_index));
  if (handle == nullptr) return 0;

  // Ownership crosses into the raw jlong handle from here; destroy() below
  // is the only path that reclaims it.
  return reinterpret_cast<jlong>(handle.release());
}

JNIEXPORT jstring JNICALL Java_org_photonvision_vkapriltag_VkAprilTagJNI_validateGeometry(
    JNIEnv *env, jclass, jint width, jint height, jint decimation) {
  if (width <= 0 || height <= 0) {
    return env->NewStringUTF("width and height must be positive");
  }
  if (decimation <= 0) {
    return env->NewStringUTF("decimation must be positive");
  }
  std::string reason;
  if (ValidateGeometry(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                       static_cast<uint32_t>(decimation), &reason)) {
    return nullptr;
  }
  return env->NewStringUTF(reason.c_str());
}

JNIEXPORT jdoubleArray JNICALL Java_org_photonvision_vkapriltag_VkAprilTagJNI_detect(
    JNIEnv *env, jclass, jlong handle_ptr, jlong gray_mat_addr) {
  DetectorHandle *handle = AsHandle(handle_ptr);
  if (handle == nullptr) {
    SetLastError("detect() called with a null handle");
    return nullptr;
  }

  // Mirrors RknnJNI's detect(long, long) convention: the second argument is
  // an OpenCV Mat's native address (Mat.getNativeObjAddr()), not a raw pixel
  // pointer - see RknnObjectDetector.detect()'s
  // letterboxed.getNativeObjAddr() call.
  auto *mat = reinterpret_cast<cv::Mat *>(gray_mat_addr);
  if (mat == nullptr || mat->empty()) {
    SetLastError("detect() called with a null or empty Mat");
    return nullptr;
  }
  if (mat->type() != CV_8UC1) {
    SetLastError("detect() requires a CV_8UC1 (single-channel 8-bit grayscale) Mat");
    return nullptr;
  }
  if (!mat->isContinuous()) {
    SetLastError("detect() requires a continuous Mat (no ROI/step gaps)");
    return nullptr;
  }

  bool ok = false;
  std::vector<double> flat = Detect(*handle, mat->data, static_cast<uint32_t>(mat->cols),
                                    static_cast<uint32_t>(mat->rows), &ok);
  if (!ok) return nullptr;

  jdoubleArray result = env->NewDoubleArray(static_cast<jsize>(flat.size()));
  if (result == nullptr) return nullptr;  // OutOfMemoryError already thrown
  env->SetDoubleArrayRegion(result, 0, static_cast<jsize>(flat.size()), flat.data());
  return result;
}

JNIEXPORT jstring JNICALL Java_org_photonvision_vkapriltag_VkAprilTagJNI_getLastError(JNIEnv *env,
                                                                                     jclass) {
  const std::string &message = LastError();
  if (message.empty()) return nullptr;
  return env->NewStringUTF(message.c_str());
}

JNIEXPORT void JNICALL Java_org_photonvision_vkapriltag_VkAprilTagJNI_destroy(JNIEnv *, jclass,
                                                                              jlong handle_ptr) {
  delete AsHandle(handle_ptr);
}

}  // extern "C"
