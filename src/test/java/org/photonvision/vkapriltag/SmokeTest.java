package org.photonvision.vkapriltag;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

/**
 * Manual smoke test for the native library - not a JUnit test, since this repo has no JUnit
 * dependency wired up yet (see the plan's R1 section). Run with:
 *
 * <pre>
 * java -Djava.library.path=&lt;dir containing the built .so/.dll&gt; \
 *      -cp build/classes org.photonvision.vkapriltag.SmokeTest [pgmPath] [family]
 * </pre>
 *
 * With no arguments, only {@link VkAprilTagJNI#isSupported} and {@link
 * VkAprilTagJNI#enumerateDevices} are exercised - both must work even with no GPU present. Pass a
 * PGM path (P5, 8-bit grayscale, width/height multiples of 8) to also exercise {@code create}/
 * {@code detect}/{@code destroy}.
 */
public final class SmokeTest {
    private SmokeTest() {}

    public static void main(String[] args) throws IOException {
        System.load(nativeLibraryPathOrThrow("vkapriltag.jni.path", "vkapriltag_jni.dll",
                "libvkapriltag_jni.so", "libvkapriltag_jni.dylib"));
        System.load(nativeLibraryPathOrThrow("vkapriltag.jni.testSupportPath",
                "vkapriltag_jni_test_support.dll", "libvkapriltag_jni_test_support.so",
                "libvkapriltag_jni_test_support.dylib"));

        System.out.println("isSupported() = " + VkAprilTagJNI.isSupported());

        VkAprilTagDeviceInfo[] devices = VkAprilTagJNI.enumerateDevices();
        System.out.println("enumerateDevices() -> " + devices.length + " device(s)");
        for (VkAprilTagDeviceInfo d : devices) {
            System.out.println("  [" + d.index() + "] " + d.name() + " - " + d.description()
                    + " (cpu=" + d.isCpuDevice() + ", apiVersion=0x"
                    + Integer.toHexString(d.apiVersion()) + ")");
        }
        String err = VkAprilTagJNI.getLastError();
        if (err != null) System.out.println("getLastError() after enumerate = " + err);

        if (args.length == 0) {
            System.out.println("No PGM path given; skipping create()/detect()/destroy().");
            return;
        }

        String pgmPath = args[0];
        String family = args.length > 1 ? args[1] : "tag36h11";

        Pgm pgm = Pgm.read(pgmPath);
        System.out.println("Loaded " + pgmPath + " (" + pgm.width + "x" + pgm.height + ")");

        long handle = VkAprilTagJNI.create(pgm.width, pgm.height, family, 0, -1);
        if (handle == 0) {
            System.out.println("create() FAILED: " + VkAprilTagJNI.getLastError());
            System.exit(1);
        }
        System.out.println("create() -> handle " + handle);

        long matAddr = OpenCvMatBridge.wrapGray(pgm.pixels, pgm.width, pgm.height);
        try {
            long t0 = System.nanoTime();
            double[] result = VkAprilTagJNI.detect(handle, matAddr);
            double detectMs = (System.nanoTime() - t0) / 1e6;
            if (result == null) {
                System.out.println("detect() FAILED: " + VkAprilTagJNI.getLastError());
                System.exit(1);
            }
            System.out.printf("detect() took %.3f ms%n", detectMs);
            int n = (int) result[0];
            System.out.println("detect() -> " + n + " tag(s)");
            for (int i = 0; i < n; i++) {
                int base = 1 + i * 22;
                int id = (int) result[base];
                int hamming = (int) result[base + 1];
                double margin = result[base + 2];
                double cx = result[base + 3];
                double cy = result[base + 4];
                System.out.printf(
                        "  tag %d: hamming=%d margin=%.3f center=(%.2f, %.2f)%n",
                        id, hamming, margin, cx, cy);
            }
        } finally {
            OpenCvMatBridge.releaseGray(matAddr);
            VkAprilTagJNI.destroy(handle);
        }
        System.out.println("destroy() done.");
    }

    private static String nativeLibraryPathOrThrow(String systemProperty, String... candidates) {
        String libraryPath = System.getProperty(systemProperty);
        if (libraryPath != null) return libraryPath;
        for (String name : candidates) {
            File f = new File(name);
            if (f.exists()) return f.getAbsolutePath();
        }
        throw new IllegalStateException(
                "Could not find a native library for any of " + String.join(", ", candidates)
                        + "; pass -D" + systemProperty + "=<path>");
    }

    /** Minimal binary PGM (P5) reader - just enough to feed test images into detect(). */
    static final class Pgm {
        final int width;
        final int height;
        final byte[] pixels;

        private Pgm(int width, int height, byte[] pixels) {
            this.width = width;
            this.height = height;
            this.pixels = pixels;
        }

        static Pgm read(String path) throws IOException {
            byte[] all = Files.readAllBytes(new File(path).toPath());
            int pos = 0;
            String magic = readToken(all, pos);
            pos = skipToken(all, pos);
            if (!magic.equals("P5")) throw new IOException("not a binary PGM (P5): " + path);
            int width = Integer.parseInt(readToken(all, pos));
            pos = skipToken(all, pos);
            int height = Integer.parseInt(readToken(all, pos));
            pos = skipToken(all, pos);
            String maxvalToken = readToken(all, pos);
            pos = skipToken(all, pos);
            if (!maxvalToken.equals("255")) {
                throw new IOException("expected maxval 255, got " + maxvalToken);
            }
            pos++; // single whitespace byte after maxval, per the PGM spec
            byte[] pixels = new byte[width * height];
            System.arraycopy(all, pos, pixels, 0, pixels.length);
            return new Pgm(width, height, pixels);
        }

        private static int skipWhitespaceAndComments(byte[] data, int pos) {
            while (pos < data.length) {
                if (data[pos] == '#') {
                    while (pos < data.length && data[pos] != '\n') pos++;
                } else if (Character.isWhitespace(data[pos])) {
                    pos++;
                } else {
                    break;
                }
            }
            return pos;
        }

        private static String readToken(byte[] data, int pos) {
            int start = skipWhitespaceAndComments(data, pos);
            int end = start;
            while (end < data.length && !Character.isWhitespace(data[end])) end++;
            return new String(data, start, end - start, java.nio.charset.StandardCharsets.US_ASCII);
        }

        private static int skipToken(byte[] data, int pos) {
            int start = skipWhitespaceAndComments(data, pos);
            int end = start;
            while (end < data.length && !Character.isWhitespace(data[end])) end++;
            return end;
        }
    }
}
