package org.photonvision.vkapriltag;

/**
 * JNI surface over {@code vkapriltag}'s {@code PoseEstimator} - a from-scratch, allocation-free
 * rewrite of libapriltag's {@code apriltag_pose.c}. Independent of {@link VkAprilTagJNI}'s
 * detector handles: a pose estimator only needs camera intrinsics and a tag size, nothing
 * detector-specific (family, resolution, device, decimation), so it has its own handle type and
 * can be created/destroyed on its own lifecycle (e.g. recreated only when calibration changes,
 * without touching detection at all).
 *
 * <p>Every method here is safe to call on a platform with no Vulkan driver at all - pose
 * estimation is pure CPU arithmetic, entirely unrelated to Vulkan. No native method throws a Java
 * exception; callers should check return values and consult {@link #getLastError} when something
 * failed.
 */
public class VkAprilTagPoseEstimatorJNI {
    private VkAprilTagPoseEstimatorJNI() {}

    /** Number of doubles making up one candidate pose: R (9) + t (3) + error + valid + iterations. */
    public static final int DOUBLES_PER_POSE = 15;

    /** Number of doubles {@link #estimatePoses} writes per tag: two back-to-back candidate poses. */
    public static final int DOUBLES_PER_TAG = 2 * DOUBLES_PER_POSE;

    /**
     * Creates a native pose estimator for one fixed (camera intrinsics, tag size) configuration.
     *
     * @param fx focal length x, pixels
     * @param fy focal length y, pixels
     * @param cx principal point x, pixels
     * @param cy principal point y, pixels
     * @param tagsize the tag's full black-border width, metres - translation scales linearly with
     *     this, so an inaccurate value here dwarfs every other error source in the solver
     * @param cpuThreads degree of parallelism for {@link #estimatePoses}; 0 selects {@code
     *     std::thread::hardware_concurrency()}, matching {@link VkAprilTagJNI#create}'s
     *     {@code cpuThreads} semantics
     * @return an opaque native handle for {@link #estimatePoses} and {@link #destroy}, or 0 on
     *     failure - check {@link #getLastError} for why
     */
    public static native long create(
            double fx, double fy, double cx, double cy, double tagsize, int cpuThreads);

    /**
     * Estimates a pose for every tag packed into {@code detectionsFlatBuffer}, which must be
     * exactly the flat buffer {@link VkAprilTagJNI#detect} returns (leading tag count at index 0,
     * then 22 doubles per tag) - only each tag's corners and homography are read from it; id/
     * hamming/margin/center are ignored. This lets a caller feed {@code detect()}'s output
     * directly into this method with no re-marshalling.
     *
     * @return null on total failure (check {@link #getLastError}); otherwise a flat buffer, {@link
     *     #DOUBLES_PER_TAG} doubles per tag in the same order as the input tags. Each tag's block
     *     is two back-to-back {@link #DOUBLES_PER_POSE}-double poses (the planar-pose ambiguity's
     *     two candidate solutions, lower-error first): R (row-major 3x3, 9 doubles), t (3
     *     doubles), error (1 double), valid (1 double, 0.0/1.0), iterations (1 double). A tag
     *     whose own pose could not be computed at all has both of its blocks zeroed with
     *     valid=0.0, without aborting the rest of the batch.
     */
    public static native double[] estimatePoses(long handle, double[] detectionsFlatBuffer);

    /**
     * The most recent error message from {@link #create} or {@link #estimatePoses} on the calling
     * thread, or null if the most recent call on this thread succeeded.
     */
    public static native String getLastError();

    /** Releases the native pose estimator. {@code handle} must not be used again after this call. */
    public static native void destroy(long handle);
}
