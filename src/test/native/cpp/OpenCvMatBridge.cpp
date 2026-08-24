// Native side of the test-only org.photonvision.vkapriltag.OpenCvMatBridge -
// see its Javadoc for why this exists and why it is not part of the
// production JNI surface.
#include <jni.h>

#include <opencv2/core.hpp>

#include "org_photonvision_vkapriltag_OpenCvMatBridge.h"

extern "C" {

JNIEXPORT jlong JNICALL Java_org_photonvision_vkapriltag_OpenCvMatBridge_wrapGray(
    JNIEnv *env, jclass, jbyteArray gray, jint width, jint height) {
  auto *mat = new cv::Mat(height, width, CV_8UC1);
  jbyte *bytes = env->GetByteArrayElements(gray, nullptr);
  std::memcpy(mat->data, bytes, static_cast<size_t>(width) * static_cast<size_t>(height));
  env->ReleaseByteArrayElements(gray, bytes, JNI_ABORT);
  return reinterpret_cast<jlong>(mat);
}

JNIEXPORT void JNICALL Java_org_photonvision_vkapriltag_OpenCvMatBridge_releaseGray(JNIEnv *,
                                                                                   jclass,
                                                                                   jlong mat_addr) {
  delete reinterpret_cast<cv::Mat *>(mat_addr);
}

}  // extern "C"
