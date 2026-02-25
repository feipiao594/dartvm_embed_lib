cmake_minimum_required(VERSION 3.21)

project(DARTVM_EMBED_LIB VERSION 0.1.0)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DARTVM_INSTALL_DARTSDK_BUNDLE
  "Install Dart SDK tools into package for external consumers"
  ON)
set(DARTSDK_ROOTDIR "${PROJECT_SOURCE_DIR}/dart-sdk" CACHE FILEPATH
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
set(DARTSDK_RUNTIME_GEN_SNAPSHOT_BIN "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/gen_snapshot" CACHE FILEPATH
    "Path to runtime-matching gen_snapshot binary that should be packaged for AOT")


function(_dartvm_require_file path_var desc)
  if(NOT EXISTS "${${path_var}}")
    message(FATAL_ERROR "Missing required ${desc}: ${${path_var}}")
  endif()
endfunction()

add_subdirectory(src)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(DARTVM_INSTALL_DARTSDK_BUNDLE)
  set(_dartsdk_bundle_sdk_dir "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/dart-sdk")
  set(_bundle_dart_bin "${_dartsdk_bundle_sdk_dir}/bin/dart")
  set(_bundle_dartaotruntime_bin "${_dartsdk_bundle_sdk_dir}/bin/dartaotruntime")
  set(_bundle_gen_kernel_snapshot "${_dartsdk_bundle_sdk_dir}/bin/snapshots/gen_kernel_aot.dart.snapshot")
  set(_bundle_vm_platform_dill "${_dartsdk_bundle_sdk_dir}/lib/_internal/vm_platform_strong.dill")

  _dartvm_require_file(_bundle_dart_bin "bundled dart binary")
  _dartvm_require_file(_bundle_dartaotruntime_bin "bundled dartaotruntime binary")
  _dartvm_require_file(_bundle_gen_kernel_snapshot "bundled gen_kernel snapshot")
  _dartvm_require_file(_bundle_vm_platform_dill "bundled vm platform dill")
  _dartvm_require_file(DARTSDK_RUNTIME_GEN_SNAPSHOT_BIN "runtime gen_snapshot")

  if(DARTVM_USE_STANDALONE_LIBDART)
    set(_bundle_jit_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/libdart_jit.a")
    set(_bundle_aot_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/libdart_aotruntime.a")
    set(_bundle_jit_extra_obj "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/elf_loader.elf_loader.o")
    set(_bundle_aot_extra_obj "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/elf_loader.virtual_memory_posix.o")
  else()
    set(_bundle_jit_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/libdart_embedder_runtime_jit_static.a")
    set(_bundle_aot_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/libdart_embedder_runtime_aot_precompiled_static.a")
    set(_bundle_jit_extra_obj "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/dart_set.dartdev_isolate.o")
    set(_bundle_aot_extra_obj "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/elf_loader.virtual_memory_posix.o")
  endif()
  set(_bundle_libcxx "${DART_DIR}/buildtools/linux-x64/clang/lib/x86_64-unknown-linux-gnu/libc++.a")
  set(_bundle_libunwind "${DART_DIR}/buildtools/linux-x64/clang/lib/x86_64-unknown-linux-gnu/libunwind.a")

  _dartvm_require_file(_bundle_jit_runtime_lib "JIT runtime static library")
  _dartvm_require_file(_bundle_aot_runtime_lib "AOT runtime static library")
  _dartvm_require_file(_bundle_jit_extra_obj "JIT runtime support object")
  _dartvm_require_file(_bundle_libcxx "libc++ static library")
  _dartvm_require_file(_bundle_libunwind "libunwind static library")

  install(DIRECTORY "${_dartsdk_bundle_sdk_dir}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dartvm_embed_lib/dart-sdk/out/${DARTSDK_BUILD_DIR}/dart-sdk"
    USE_SOURCE_PERMISSIONS
  )
  install(PROGRAMS "${DARTSDK_RUNTIME_GEN_SNAPSHOT_BIN}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dartvm_embed_lib/runtime/${DARTSDK_BUILD_DIR}")
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
