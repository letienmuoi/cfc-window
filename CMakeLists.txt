cmake_minimum_required(VERSION 3.20)

project(cfc-screen-receiver LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# ─── FetchContent for all dependencies from GitHub ───────────────────────────
include(FetchContent)

# ─── OpenCV from GitHub (build from source, minimal modules only) ────────────
# Disable most modules to speed up the build - we only need core/imgproc/imgcodecs/photo/highgui
set(BUILD_LIST "core,imgproc,imgcodecs,photo,highgui,videoio" CACHE STRING "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_js OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python2 OFF CACHE BOOL "" FORCE)
set(BUILD_JAVA OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_java OFF CACHE BOOL "" FORCE)
set(WITH_PROTOBUF OFF CACHE BOOL "" FORCE)
set(WITH_QUIRC OFF CACHE BOOL "" FORCE)
set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
set(WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
set(WITH_DSHOW ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_STATIC_CRT OFF CACHE BOOL "" FORCE)
set(OPENCV_GENERATE_PKGCONFIG OFF CACHE BOOL "" FORCE)
# Disable dnn because it pulls in protobuf which can cause build issues
set(BUILD_opencv_dnn OFF CACHE BOOL "" FORCE)
set(WITH_JASPER OFF CACHE BOOL "" FORCE)
set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
set(WITH_WEBP OFF CACHE BOOL "" FORCE)
set(WITH_TIFF OFF CACHE BOOL "" FORCE)
set(WITH_OPENJPEG OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    opencv
    GIT_REPOSITORY https://github.com/opencv/opencv.git
    GIT_TAG        4.11.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(opencv)

# ─── libcimbar from GitHub ───────────────────────────────────────────────────
FetchContent_Declare(
    libcimbar
    GIT_REPOSITORY https://github.com/sz3/libcimbar.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# We don't want libcimbar's executables or tests, just the libraries
set(DISABLE_TESTS TRUE CACHE BOOL "" FORCE)

FetchContent_GetProperties(libcimbar)
if(NOT libcimbar_POPULATED)
    FetchContent_Populate(libcimbar)
endif()

# Set OPENCV_LIBS so libcimbar's CMake can pick it up
set(OPENCV_LIBS opencv_imgcodecs opencv_imgproc opencv_photo opencv_core)

# Set CPPFILESYSTEM to empty for MSVC (std::filesystem is built-in)
set(CPPFILESYSTEM "" CACHE STRING "" FORCE)

# ─── Add libcimbar subdirectories manually ────────────────────────────────
# We manually add its source directories to avoid its root CMakeLists.txt
# which calls find_package(OpenCV REQUIRED) and tries to build its executables.
include_directories(
    ${libcimbar_SOURCE_DIR}/src/lib
    ${libcimbar_SOURCE_DIR}/src/third_party_lib
    ${opencv_SOURCE_DIR}/include
    ${opencv_SOURCE_DIR}/modules/core/include
    ${opencv_SOURCE_DIR}/modules/imgproc/include
    ${opencv_SOURCE_DIR}/modules/imgcodecs/include
    ${opencv_SOURCE_DIR}/modules/photo/include
    ${opencv_SOURCE_DIR}/modules/highgui/include
    ${opencv_SOURCE_DIR}/modules/videoio/include
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(CIMBAR_LIBS
    src/lib/bit_file
    src/lib/chromatic_adaptation
    src/lib/cimb_translator
    src/lib/cimbar_js
    src/lib/compression
    src/lib/encoder
    src/lib/extractor
    src/lib/fountain
    src/lib/gui
    src/lib/image_hash
    src/lib/serialize
    src/lib/util

    src/third_party_lib/base91
    src/third_party_lib/cxxopts
    src/third_party_lib/intx
    src/third_party_lib/libcorrect
    src/third_party_lib/libpopcnt
    src/third_party_lib/wirehair
    src/third_party_lib/zstd
)

foreach(proj ${CIMBAR_LIBS})
    add_subdirectory(${libcimbar_SOURCE_DIR}/${proj} ${CMAKE_BINARY_DIR}/libcimbar_build/${proj})
endforeach()

# ─── Our application ────────────────────────────────────────────────────────
add_executable(cfc-screen-receiver
    main.cpp
    ScreenCapturer.cpp
    DecoderThread.cpp
)

target_include_directories(cfc-screen-receiver PRIVATE
    ${libcimbar_SOURCE_DIR}/src/lib
    ${libcimbar_SOURCE_DIR}/src/third_party_lib
    ${CMAKE_CURRENT_SOURCE_DIR}/concurrent
)

target_link_libraries(cfc-screen-receiver PRIVATE
    # libcimbar libraries
    cimb_translator
    extractor
    encoder
    fountain
    compression
    bit_file
    chromatic_adaptation
    image_hash
    serialize
    util

    # third-party from libcimbar
    correct_static
    wirehair
    zstd

    # OpenCV
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_photo
    opencv_highgui
    opencv_videoio

    # Windows APIs for screen capture
    d3d11
    dxgi
    RuntimeObject
    windowsapp
    Gdi32
    User32
)

target_compile_definitions(cfc-screen-receiver PRIVATE
    ZSTD_STATIC_LINKING_ONLY
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
)
