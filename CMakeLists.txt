cmake_minimum_required(VERSION 3.21)

project(DARTVM_EMBED_LIB VERSION 0.1.0)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DARTVM_AUTO_PREPARE_SDK
  "Run build_dart_sdk.py from CMake before building library variants"
  OFF)
set(DARTVM_PREPARE_SDK_BUILD_TYPE "release" CACHE STRING
    "Build type used by build_dart_sdk.py (debug/release/all)")
set_property(CACHE DARTVM_PREPARE_SDK_BUILD_TYPE PROPERTY STRINGS debug release all)
set(DARTVM_PREPARE_SDK_TARGETS "libdart_embedder" CACHE STRING
    "Semicolon-separated tools/build.py targets passed to build_dart_sdk.py")

option(DARTVM_INSTALL_DARTSDK_BUNDLE
  "Install Dart SDK tools into package for external consumers"
  ON)
set(DARTSDK_ROOTDIR "${PROJECT_SOURCE_DIR}/../dart-sdk" CACHE FILEPATH
    "Directory that dart-sdk is cloned to")
set(DARTSDK_BUILD_DIR "ReleaseX64" CACHE STRING
    "Dart SDK out dir name under sdk/out")

option(DARTVM_USE_STANDALONE_LIBDART
  "Link against standalone libdart.a instead of libdart_embedder_runtime_*"
  OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(DARTVM_EMBED_LIB_DIR "${PROJECT_SOURCE_DIR}/src")
set(DART_DIR "${DARTSDK_ROOTDIR}/sdk")

set(_default_dart_bin "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/dart")
set(_default_create_sdk_dart_bin
    "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/dart-sdk/bin/dart")
if(EXISTS "${_default_create_sdk_dart_bin}")
  set(_resolved_dart_bin "${_default_create_sdk_dart_bin}")
else()
  set(_resolved_dart_bin "")
endif()

set(DARTSDK_DART_BIN "${_resolved_dart_bin}" CACHE FILEPATH
    "Path to dart executable used to compile example kernel files")
if(BUILD_SAMPLES AND NOT EXISTS "${DARTSDK_DART_BIN}")
  message(FATAL_ERROR
    "DARTSDK_DART_BIN is missing. Build a matching SDK first (./tools/build.py -m release create_sdk) "
    "or pass -DDARTSDK_DART_BIN=<path-to-matching-dart>.")
endif()
if(BUILD_SAMPLES AND DARTSDK_DART_BIN MATCHES ".*/out/[^/]+/dart$")
  message(FATAL_ERROR
    "DARTSDK_DART_BIN points to a raw runtime binary (${DARTSDK_DART_BIN}) which cannot run 'dart compile kernel'. "
    "Use the SDK CLI binary: .../out/${DARTSDK_BUILD_DIR}/dart-sdk/bin/dart")
endif()
set(DARTSDK_GEN_SNAPSHOT_BIN "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/gen_snapshot" CACHE FILEPATH
    "Path to gen_snapshot executable used to build AOT snapshots for examples")

if(DARTVM_AUTO_PREPARE_SDK)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_prepare_cmd
    ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/build_dart_sdk.py"
    --build-type "${DARTVM_PREPARE_SDK_BUILD_TYPE}"
    --dart-sdk-dir "${DARTSDK_ROOTDIR}"
  )
  foreach(_target IN LISTS DARTVM_PREPARE_SDK_TARGETS)
    list(APPEND _prepare_cmd --target "${_target}")
  endforeach()

  add_custom_target(prepare_dart_sdk
    COMMAND ${CMAKE_COMMAND} -E chdir "${PROJECT_SOURCE_DIR}/.." ${_prepare_cmd}
    COMMENT "Preparing dart-sdk + runtime artifacts via build_dart_sdk.py"
    VERBATIM
  )
endif()

add_subdirectory(src)

if(TARGET prepare_dart_sdk)
  add_dependencies(dartvm_embed_lib_jit prepare_dart_sdk)
  add_dependencies(dartvm_embed_lib_aot prepare_dart_sdk)
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(DARTVM_INSTALL_DARTSDK_BUNDLE)
  set(_dartsdk_bundle_dir "${DART_DIR}")
  if(NOT EXISTS "${_dartsdk_bundle_dir}/out/${DARTSDK_BUILD_DIR}/dart-sdk/bin/dart")
    message(FATAL_ERROR
      "DARTVM_INSTALL_DARTSDK_BUNDLE=ON but dart-sdk bundle not found at ${_dartsdk_bundle_dir}")
  endif()
  install(DIRECTORY "${_dartsdk_bundle_dir}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dartvm_embed_lib/dart-sdk"
    USE_SOURCE_PERMISSIONS
  )
endif()

install(EXPORT dartvm_embed_libTargets
  FILE dartvm_embed_libTargets.cmake
  NAMESPACE DartVmEmbed::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dartvm_embed_lib"
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/dartvm_embed_libConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/dartvm_embed_libConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dartvm_embed_lib"
)

write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/dartvm_embed_libConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${PROJECT_BINARY_DIR}/dartvm_embed_libConfig.cmake"
  "${PROJECT_BINARY_DIR}/dartvm_embed_libConfigVersion.cmake"
  "${PROJECT_SOURCE_DIR}/cmake/DartVmEmbedHelpers.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dartvm_embed_lib"
)
