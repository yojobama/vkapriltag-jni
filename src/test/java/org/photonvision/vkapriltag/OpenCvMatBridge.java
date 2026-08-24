package org.photonvision.vkapriltag;

/**
 * Test-only helper: wraps a raw grayscale pixel buffer in a heap-allocated {@code cv::Mat} and
 * hands back its native address, exactly the shape {@link VkAprilTagJNI#detect} expects.
 *
 * <p>Not part of the production JNI surface - real PhotonVision callers already have a live
 * {@code org.opencv.core.Mat} from the WPILib OpenCV artifact and call {@code
 * Mat.getNativeObjAddr()} directly (see {@code RknnObjectDetector.detect()} for the existing
 * precedent this mirrors). This class exists only so {@link SmokeTest} can exercise {@code
 * detect()} without pulling in the full opencv-java jar.
 */
final class OpenCvMatBridge {
    private OpenCvMatBridge() {}

    /** Allocates a {@code CV_8UC1} Mat of the given size, copies {@code gray} into it. */
    static native long wrapGray(byte[] gray, int width, int height);

    /** Frees a Mat previously returned by {@link #wrapGray}. */
    static native void releaseGray(long matAddr);
}
