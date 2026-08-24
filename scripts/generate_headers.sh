#!/usr/bin/env bash
# Compiles the Java sources and emits the JNI headers VkAprilTagJNI.cpp
# includes by name, into <build_dir>/jniheaders. Run this before the first
# `cmake -B <build_dir>` (and again whenever a native method's signature
# changes) - CMakeLists.txt checks for these headers at configure time and
# fails with a pointer back here if they're missing.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_root/build}"

mkdir -p "$build_dir/classes" "$build_dir/jniheaders"
javac -h "$build_dir/jniheaders" -d "$build_dir/classes" \
  "$repo_root"/src/main/java/org/photonvision/vkapriltag/*.java

echo "Generated JNI headers in $build_dir/jniheaders"
