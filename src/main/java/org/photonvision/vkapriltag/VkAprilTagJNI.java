package org.photonvision.vkapriltag;

/**
 * JNI surface over the {@code vkapriltag} Vulkan-compute AprilTag detector. Mirrors the shape of
 * PhotonVision's other native detector bindings (e.g. {@code RknnJNI}): a long-lived native
 * handle created once per detector configuration, destroyed explicitly, with per-frame detection
 * calls in between.
 *
 * <p>Every method here is safe to call on a platform with no Vulkan driver at all - {@link
 * #isSupported} and {@link #enumerateDevices} never throw, and {@link #create} returns 0 on
 * failure rather than throwing. No native method throws a Java exception; callers should check
 * return values and consult {@link #getLastError} when something failed.
 */
public class VkAprilTagJNI {
    private VkAprilTagJNI() {}

    /**
     * Cheap, instance-only probe for whether a hardware Vulkan 1.1+ device is available. Never
     * throws. Safe to call before any detector has been created, and safe on a machine with no
     * GPU driver installed at all.
     */
    public static native boolean isSupported();

    /**
     * Enumerates the Vulkan physical devices visible to the native library, without creating any
     * of them. Safe to call at any time, including before {@link #isSupported} has been checked -
     * both go through the same static, allocation-only probe on the native side.
     */
    public static native VkAprilTagDeviceInfo[] enumerateDevices();

    /**
     * Creates a native detector for one fixed (width, height, family) configuration. The family's
     * border polarity is read from the tag family itself; there is no separate parameter for it.
     *
     * @param width frame width in pixels; must be a multiple of 8
     * @param height frame height in pixels; must be a multiple of 8
     * @param family an AprilTag family name recognized by the fetched {@code apriltag} C library,
     *     e.g. {@code "tag36h11"} or {@code "tag16h5"}
     * @param cpuThreads degree of parallelism for the CPU tail (quad fitting); 0 selects {@code
     *     std::thread::hardware_concurrency()}, 1 is fully serial
     * @param deviceIndex a raw index from {@link #enumerateDevices}, or -1 to let the library
     *     auto-select (discrete &gt; integrated &gt; virtual &gt; CPU)
     * @return an opaque native handle for {@link #detect} and {@link #destroy}, or 0 on failure -
     *     check {@link #getLastError} for why
     */
    public static native long create(
            int width, int height, String family, int cpuThreads, int deviceIndex);

    /**
     * Runs one frame of detection.
     *
     * @param handle a handle returned by {@link #create}
     * @param grayMatAddr the native address of a continuous, {@code CV_8UC1} OpenCV {@code Mat}
     *     matching this detector's configured width/height (i.e. {@code Mat.getNativeObjAddr()})
     * @return null on failure (check {@link #getLastError}); otherwise a flat buffer: {@code [0]}
     *     is the tag count N, followed by 22 doubles per tag - id, hamming, decision margin,
     *     center x/y, 4 corners as x0,y0,x1,y1,x2,y2,x3,y3, then the row-major 3x3 homography
     */
    public static native double[] detect(long handle, long grayMatAddr);

    /**
     * The most recent error message from {@link #create} or {@link #detect} on the calling
     * thread, or null if the most recent call on this thread succeeded. Native errors are stored
     * thread-locally, matching the fact that a single detector handle is only ever driven from one
     * {@code VisionRunner} thread.
     */
    public static native String getLastError();

    /** Releases the native detector. {@code handle} must not be used again after this call. */
    public static native void destroy(long handle);
}
