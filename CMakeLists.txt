cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(vkapriltag_jni VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# vkapriltag itself. Pinned to a tag, not a branch, so a CI build today and a
# CI build next year fetch the same source. VKAPRILTAG_EMBED_SHADERS=ON is
# what makes this library usable from a .so with no install prefix at all -
# see vkapriltag's EmbeddedShaders.h for why that matters to a JNI shim
# specifically.
# ---------------------------------------------------------------------------
set(VKAPRILTAG_GIT_TAG "v1.3.1" CACHE STRING
    "vkapriltag tag to fetch")
option(VKAPRILTAG_JNI_LOCAL_SOURCE
       "Build against a local vkapriltag checkout instead of fetching one (VKAPRILTAG_JNI_LOCAL_SOURCE_DIR)"
       OFF)
set(VKAPRILTAG_JNI_LOCAL_SOURCE_DIR "" CACHE PATH
    "Path to a local vkapriltag/apriltags_vulkan checkout, used when VKAPRILTAG_JNI_LOCAL_SOURCE=ON")

set(VKAPRILTAG_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(VKAPRILTAG_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(VKAPRILTAG_EMBED_SHADERS ON CACHE BOOL "" FORCE)

# vkapriltag builds as a static lib that ends up linked into
# libvkapriltag_jni.so below, so it must be position-independent - without
# this, x86-64 linking fails with "relocation ... can not be used when
# making a shared object; recompile with -fPIC" (arm64 tolerates it, x86-64
# doesn't). Must be set before FetchContent_MakeAvailable/add_subdirectory
# so it applies to that target too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# vkapriltag's own AprilRobotics/apriltag FetchContent dependency defaults
# to BUILD_SHARED_LIBS=ON upstream, which gives libvkapriltag_jni.so an
# extra DT_NEEDED on libapriltag.so.3 that nothing in this repo's packaging
# ships - CI's smoke test failed with exactly that UnsatisfiedLinkError.
# Forcing static here (before the fetch, so it applies to that nested
# dependency too) embeds apriltag directly, matching the "one self-
# contained .so, no runtime deps beyond WPILib's own libs" design this
# whole native shim exists for.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(VKAPRILTAG_JNI_LOCAL_SOURCE)
  if(NOT VKAPRILTAG_JNI_LOCAL_SOURCE_DIR)
    message(FATAL_ERROR "VKAPRILTAG_JNI_LOCAL_SOURCE=ON requires VKAPRILTAG_JNI_LOCAL_SOURCE_DIR")
  endif()
  add_subdirectory(${VKAPRILTAG_JNI_LOCAL_SOURCE_DIR} vkapriltag-build)
else()
  include(FetchContent)
  FetchContent_Declare(
    vkapriltag
    GIT_REPOSITORY https://github.com/yojobama/vkapriltag.git
    GIT_TAG ${VKAPRILTAG_GIT_TAG}
    SOURCE_SUBDIR apriltags_vulkan
  )
  FetchContent_MakeAvailable(vkapriltag)
endif()

# ---------------------------------------------------------------------------
# OpenCV: needed only for the cv::Mat type definition detect() reinterpret-
# casts its jlong argument into (mirroring how PhotonVision's other JNI
# detectors, e.g. RknnJNI, cross the boundary - see
# RknnObjectDetector.detect()'s getNativeObjAddr() call). No OpenCV linking
# is required (Mat::type()/isContinuous()/empty() are header-inline), but the
# struct LAYOUT must match photon-core's OpenCV build exactly, since this is
# a raw pointer reinterpretation across a process boundary that only works
# because both sides agree on the same ABI. In the real PhotonVision build,
# that means this must link against the identical wpilibOpenCv artifact
# version photon-core uses, not merely "some OpenCV" - a version mismatch
# here is an ABI bug, not a build error.
#
# find_package(OpenCV) only works against a real CMake package (an
# OpenCVConfig.cmake) - a system OpenCV install has one, but the raw WPILib
# artifact (org.wpilib.thirdparty.opencv:opencv-cpp) does not: it's just a
# headers/ zip and a linux/arm64/shared/*.so zip with nothing else, matching
# how photon-core itself consumes it (as a wpilibNatives dependency, not a
# find_package()-able install). So there are two paths here, and only one of
# them is safe for a real build:
#
# - WPILIB_OPENCV_INCLUDE_DIR + WPILIB_OPENCV_LIB_DIR set (both, together):
#   builds an imported target directly from those paths, no find_package()
#   involved. This is the ONLY path that can actually produce an
#   ABI-correct artifact - see scripts/ci_fetch_wpilib_opencv.sh for how CI
#   populates these from the exact opencv-cpp version photon-core's root
#   build.gradle currently resolves.
# - Neither set: falls back to find_package(OpenCV REQUIRED COMPONENTS
#   core) against whatever OpenCV is installed on the machine. Convenient
#   for local iteration (matches every command run by hand earlier in this
#   project's history), but the resulting .so must never be shipped to
#   PhotonVision - only ever used for local smoke-testing detect() logic
#   against a throwaway cv::Mat.
if(WPILIB_OPENCV_INCLUDE_DIR AND WPILIB_OPENCV_LIB_DIR)
  file(GLOB _wpilib_opencv_core_so "${WPILIB_OPENCV_LIB_DIR}/libopencv_core.so.*")
  list(LENGTH _wpilib_opencv_core_so _wpilib_opencv_core_so_count)
  if(NOT _wpilib_opencv_core_so_count EQUAL 1)
    message(FATAL_ERROR
      "Expected exactly one libopencv_core.so.* under WPILIB_OPENCV_LIB_DIR="
      "${WPILIB_OPENCV_LIB_DIR}, found ${_wpilib_opencv_core_so_count}: ${_wpilib_opencv_core_so}")
  endif()

  add_library(wpilib_opencv_core SHARED IMPORTED)
  set_target_properties(wpilib_opencv_core PROPERTIES
    IMPORTED_LOCATION "${_wpilib_opencv_core_so}"
    INTERFACE_INCLUDE_DIRECTORIES "${WPILIB_OPENCV_INCLUDE_DIR}"
  )
  set(OpenCV_LIBS wpilib_opencv_core)
  message(STATUS "Using WPILib-vendored OpenCV: ${_wpilib_opencv_core_so}")
elseif(WPILIB_OPENCV_INCLUDE_DIR OR WPILIB_OPENCV_LIB_DIR)
  message(FATAL_ERROR
    "WPILIB_OPENCV_INCLUDE_DIR and WPILIB_OPENCV_LIB_DIR must both be set, or neither")
else()
  find_package(OpenCV REQUIRED COMPONENTS core)
  message(WARNING
    "Building against system OpenCV (found: ${OpenCV_VERSION}), not the WPILib-vendored "
    "build photon-core actually links - this .so has a real ABI risk and must not be "
    "shipped. Set -DWPILIB_OPENCV_INCLUDE_DIR/-DWPILIB_OPENCV_LIB_DIR for a real build.")
endif()

# Deliberately not find_package(JNI) - that module's default REQUIRED
# behavior also demands libjawt/libjvm (AWT/JVM components), which a headless
# JDK (exactly what a PhotonVision coprocessor runs) doesn't ship, and which
# a JNI shared library has no business linking against anyway: the JVM that
# loads this .so already has those symbols resolved, and a "guest" native
# library only ever needs the jni.h/jni_md.h headers, never a link
# dependency on the JVM itself.
if(NOT JAVA_HOME)
  message(FATAL_ERROR "JAVA_HOME must be set (points at a JDK, not just a JRE)")
endif()
if(WIN32)
  set(JNI_MD_SUBDIR win32)
elseif(APPLE)
  set(JNI_MD_SUBDIR darwin)
else()
  set(JNI_MD_SUBDIR linux)
endif()
set(JNI_INCLUDE_DIRS "${JAVA_HOME}/include" "${JAVA_HOME}/include/${JNI_MD_SUBDIR}")
if(NOT EXISTS "${JAVA_HOME}/include/jni.h")
  message(FATAL_ERROR "No jni.h under ${JAVA_HOME}/include - is JAVA_HOME a JDK (not a JRE)?")
endif()

add_library(vkapriltag_jni SHARED
  src/main/native/cpp/DetectorHandle.cpp
  src/main/native/cpp/VkAprilTagJNI.cpp
  src/main/native/cpp/PoseEstimatorHandle.cpp
  src/main/native/cpp/VkAprilTagPoseEstimatorJNI.cpp
)

target_include_directories(vkapriltag_jni PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/jniheaders
  ${JNI_INCLUDE_DIRS}
)

target_link_libraries(vkapriltag_jni PRIVATE
  vkapriltag::vkapriltag
  ${OpenCV_LIBS}
  
)

target_compile_definitions(vkapriltag_jni PRIVATE VKAPRILTAG_JNI_VERSION="${PROJECT_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vkapriltag_jni PRIVATE -Wall -Wextra -Wno-unused-parameter
                                                -fvisibility=hidden -fvisibility-inlines-hidden)
  # Belt-and-suspenders alongside JNIEXPORT's per-symbol default visibility
  # (see VkAprilTagJNI.cpp's header comment): explicitly hides every symbol
  # this .so does not intend to export, most importantly libapriltag's
  # zarray_*/matd_*/apriltag_* globals, which WPILib's own (unpatched,
  # differently built) apriltag native library also defines. Whichever of
  # the two loads first wins global symbol resolution on Linux, so letting
  # this library's copy leak would risk PhotonVision's WPILib AprilTag
  # detector silently calling into vkapriltag's patched apriltag instead, or
  # vice versa.
  target_link_options(vkapriltag_jni PRIVATE
    -Wl,--exclude-libs,ALL
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/main/native/cpp/exports.map
  )
  set_target_properties(vkapriltag_jni PROPERTIES LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main/native/cpp/exports.map)

  # RUNPATH $ORIGIN, not an absolute path: without this, linking against
  # wpilib_opencv_core (an IMPORTED target with an absolute build-tree
  # IMPORTED_LOCATION, and no install() step here to fix that up) bakes the
  # CI runner's own ephemeral build path into the shipped .so, which
  # obviously doesn't exist on a PhotonVision coprocessor. $ORIGIN is correct
  # instead because CombinedRuntimeLoader extracts every native resource -
  # including WPILib's own libopencv_core.so.NNN - into one flat directory,
  # so "next to me" is genuinely where the real one ends up at runtime,
  # regardless of which machine built this .so.
  #
  # BUILD_RPATH the target property, not a raw -Wl,-rpath linker flag: CMake
  # separately auto-appends every linked IMPORTED target's own directory to
  # the RPATH by default (verified - a raw flag lands as
  # RUNPATH=[$ORIGIN:/absolute/ci/build/path], the absolute half exactly as
  # unusable as if $ORIGIN had never been added). Setting this property is
  # what actually replaces CMake's computed RPATH instead of appending to it.
  set_target_properties(vkapriltag_jni PROPERTIES
    SKIP_BUILD_RPATH FALSE
    BUILD_WITH_INSTALL_RPATH TRUE
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
endif()

# ---------------------------------------------------------------------------
# Test-only support library for SmokeTest (src/test/java) - never shipped to
# PhotonVision, never linked against vkapriltag, exists solely so the smoke
# test can build a cv::Mat without pulling in the full opencv-java jar. See
# OpenCvMatBridge's Javadoc.
# ---------------------------------------------------------------------------
option(VKAPRILTAG_JNI_BUILD_TEST_SUPPORT "Build the SmokeTest native support library" ON)
if(VKAPRILTAG_JNI_BUILD_TEST_SUPPORT)
  add_library(vkapriltag_jni_test_support SHARED
    src/test/native/cpp/OpenCvMatBridge.cpp
  )
  target_include_directories(vkapriltag_jni_test_support PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/jniheaders
    ${JNI_INCLUDE_DIRS}
  )
  target_link_libraries(vkapriltag_jni_test_support PRIVATE ${OpenCV_LIBS} )
endif()

# javac -h places the generated JNI headers here; VkAprilTagJNI.cpp includes
# them by name. See build.gradle for how the real build drives javac, and
# scripts/generate_headers.sh for how a plain CMake build does the same.
foreach(_expected_header
    org_photonvision_vkapriltag_VkAprilTagJNI.h
    org_photonvision_vkapriltag_VkAprilTagPoseEstimatorJNI.h)
  if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/jniheaders/${_expected_header})
    message(FATAL_ERROR
      "JNI header ${_expected_header} not found under ${CMAKE_CURRENT_BINARY_DIR}/jniheaders - "
      "run scripts/generate_headers.cmake (or javac -h) before configuring, or point "
      "-DCMAKE_BINARY_DIR at a directory that already has them.")
  endif()
endforeach()
