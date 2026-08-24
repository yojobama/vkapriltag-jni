#!/usr/bin/env bash
# Builds vkapriltag_jni for the current machine and installs it into
# mavenLocal() (~/.m2/repository) in the same Maven layout a real
# maven.photonvision.org publish would use, so photon-core's build.gradle can
# depend on it with an ordinary wpilibNatives/implementation declaration
# instead of any manual file-copying step.
#
# This exists because that manual step is exactly what was done once by hand
# to prove the integration end-to-end on real hardware, and it silently does
# NOT survive a fresh checkout, a `./gradlew clean`, or Gradle re-running
# assembleNativeResourcesMain - that task rebuilds its output directory purely
# from declared dependencies, so anything dropped into it by hand disappears
# the next time it runs. Installing into mavenLocal() instead makes the
# dependency a normal, durable one.
#
# Usage: install_local_maven.sh [version]
#   version defaults to 0.1.0-local - must match photon-core's build.gradle
#   `ext.vkAprilTagVersion`.
#
# Prerequisites: this repo already built once the normal way (see README/
# CMakeLists.txt - a `build/` directory with vkapriltag-build/library/
# libvkapriltag.a and _deps/apriltag-build/libapriltag.a), and a JDK (for
# javac/jar). Only linuxarm64 is supported by this script today - see
# VkAprilTagJNI.cpp's detect() for why the OpenCV headers/lib must be the
# exact WPILib-vendored ones photon-core links, not any other OpenCV install.
set -euo pipefail

VERSION="${1:-0.1.0-local}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if [ ! -f "$BUILD_DIR/vkapriltag-build/library/libvkapriltag.a" ]; then
  echo "error: $BUILD_DIR/vkapriltag-build/library/libvkapriltag.a not found." >&2
  echo "Build this repo the normal way first (see CMakeLists.txt)." >&2
  exit 1
fi

: "${JAVA_HOME:?set JAVA_HOME to a JDK (javac/jar must be under \$JAVA_HOME/bin)}"

# Locate the exact WPILib-vendored OpenCV this photon-core build resolved,
# so the .so this script produces reads photon-core's own cv::Mat correctly.
# There is no clean Gradle API for "give me that path" from a shell script,
# so this greps it out of the transform cache - fragile, but it only needs to
# run once per machine/Gradle-cache-clear.
OPENCV_HEADERS="$(find "$HOME/.gradle/caches" -type d -path '*opencv-cpp-*-headers/opencv2' -printf '%h\n' 2>/dev/null | head -1)"
OPENCV_LIB_DIR="$(find "$HOME/.gradle/caches" -type f -name 'libopencv_core.so.*' -printf '%h\n' 2>/dev/null | grep -v python | head -1)"
if [ -z "$OPENCV_HEADERS" ] || [ -z "$OPENCV_LIB_DIR" ]; then
  echo "error: could not find the WPILib OpenCV artifact in ~/.gradle/caches." >&2
  echo "Run a photon-core build (even one that fails later) first, so Gradle fetches it." >&2
  exit 1
fi
OPENCV_CORE_SO="$(basename "$(ls "$OPENCV_LIB_DIR"/libopencv_core.so.* | head -1)")"
echo "Using OpenCV headers: $OPENCV_HEADERS"
echo "Using OpenCV lib:     $OPENCV_LIB_DIR/$OPENCV_CORE_SO"

g++ -std=c++20 -O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
  -I "$OPENCV_HEADERS" \
  -I "$JAVA_HOME/include" -I "$JAVA_HOME/include/linux" \
  -I "$BUILD_DIR/jniheaders" \
  -I "$REPO_ROOT/../vkapriltag/apriltags_vulkan/library/include" \
  -I "$BUILD_DIR/_deps/apriltag-src" \
  -c "$REPO_ROOT/src/main/native/cpp/VkAprilTagJNI.cpp" -o "$WORK/VkAprilTagJNI.o"

g++ -std=c++20 -O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
  -I "$REPO_ROOT/../vkapriltag/apriltags_vulkan/library/include" \
  -I "$BUILD_DIR/_deps/apriltag-src" \
  -c "$REPO_ROOT/src/main/native/cpp/DetectorHandle.cpp" -o "$WORK/DetectorHandle.o"

# RUNPATH is $ORIGIN, not the OpenCV cache path above: at runtime
# CombinedRuntimeLoader extracts every native resource (including WPILib's
# own libopencv_core.so.413) into one flat directory, so "next to me" is
# where the real one will actually be - this keeps the artifact independent
# of any one machine's Gradle cache layout.
g++ -shared -fPIC -o "$WORK/libvkapriltag_jni.so" \
  "$WORK/VkAprilTagJNI.o" "$WORK/DetectorHandle.o" \
  "$BUILD_DIR/vkapriltag-build/library/libvkapriltag.a" \
  "$BUILD_DIR/_deps/apriltag-build/libapriltag.a" \
  -L"$OPENCV_LIB_DIR" "-l:$OPENCV_CORE_SO" \
  -lvulkan -lpthread \
  -Wl,--exclude-libs,ALL \
  -Wl,--version-script="$REPO_ROOT/src/main/native/cpp/exports.map" \
  '-Wl,-rpath,$ORIGIN'

echo "Verifying exported symbol surface..."
UNEXPECTED="$(nm -D --defined-only "$WORK/libvkapriltag_jni.so" | grep -v -E 'Java_org_photonvision_vkapriltag_|JNI_OnLoad' || true)"
if [ -n "$UNEXPECTED" ]; then
  echo "error: unexpected exported symbols (visibility hardening failed):" >&2
  echo "$UNEXPECTED" >&2
  exit 1
fi

mkdir -p "$WORK/jnistage/linux/arm64/shared"
cp "$WORK/libvkapriltag_jni.so" "$WORK/jnistage/linux/arm64/shared/"
( cd "$WORK/jnistage" && "$JAVA_HOME/bin/jar" --create \
    --file="$WORK/vkapriltag_jni-jni-$VERSION-linuxarm64.jar" linux )

mkdir -p "$WORK/javastage"
"$JAVA_HOME/bin/javac" -d "$WORK/javastage" \
  "$REPO_ROOT"/src/main/java/org/photonvision/vkapriltag/*.java
( cd "$WORK/javastage" && "$JAVA_HOME/bin/jar" --create \
    --file="$WORK/vkapriltag_jni-java-$VERSION.jar" org/photonvision/vkapriltag )

M2=${MAVEN_LOCAL_REPO:-"$HOME/.m2/repository"}/org/photonvision

# packaging=pom, not jar, for the -jni artifact: it has no unclassified
# default jar, only the classifier-attached one above. Gradle's plain-Maven
# (no Gradle Module Metadata) classifier resolution reports an unhelpful
# "Could not find ..." across every configured repository - mavenLocal()
# included - if the POM claims packaging=jar with no matching unclassified
# jar present. Cost a fair bit of debugging to find; don't reintroduce it.
install_pom() {
  local artifact="$1"
  local packaging="$2"
  local dir="$M2/$artifact/$VERSION"
  mkdir -p "$dir"
  cat > "$dir/$artifact-$VERSION.pom" <<POM
<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0">
  <modelVersion>4.0.0</modelVersion>
  <groupId>org.photonvision</groupId>
  <artifactId>$artifact</artifactId>
  <version>$VERSION</version>
  <packaging>$packaging</packaging>
</project>
POM
}

install_pom vkapriltag_jni-jni pom
install_pom vkapriltag_jni-java jar

cp "$WORK/vkapriltag_jni-jni-$VERSION-linuxarm64.jar" "$M2/vkapriltag_jni-jni/$VERSION/"
cp "$WORK/vkapriltag_jni-java-$VERSION.jar" "$M2/vkapriltag_jni-java/$VERSION/"

echo "Installed org.photonvision:vkapriltag_jni-{jni,java}:$VERSION to $M2"
echo "Set ext.vkAprilTagVersion = \"$VERSION\" in the photonvision-vkapriltag fork's root build.gradle to match."
