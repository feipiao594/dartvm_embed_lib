cmake_minimum_required(VERSION 3.21)
set(EXPORT_COMPILE_COMMANDS ON)

if(NOT EXISTS "${DART_DIR}/runtime/include/dart_api.h")
  message(FATAL_ERROR "Missing Dart SDK headers under ${DART_DIR}/runtime/include")
endif()

function(_dartvm_embed_configure_variant flavor out_runtime_lib out_extra_objs)
  if(NOT flavor STREQUAL "jit" AND NOT flavor STREQUAL "aot")
    message(FATAL_ERROR "Invalid flavor: ${flavor}")
  endif()

  set(_runtime_extra_objects)
  if(DARTVM_USE_STANDALONE_LIBDART)
    if(flavor STREQUAL "aot")
      set(_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/libdart_aotruntime.a")
    else()
      set(_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/libdart_jit.a")
    endif()
    set(_runtime_extra_objects
      "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/elf_loader.elf_loader.o"
      "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/elf_loader.virtual_memory_posix.o"
    )
  else()
    if(flavor STREQUAL "aot")
      set(_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/libdart_embedder_runtime_aot_precompiled_static.a")
    else()
      set(_runtime_lib "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/libdart_embedder_runtime_jit_static.a")
      set(_runtime_extra_objects
        "${DART_DIR}/out/${DARTSDK_BUILD_DIR}/obj/runtime/bin/dart_set.dartdev_isolate.o"
      )
    endif()
  endif()

  if(NOT EXISTS "${_runtime_lib}")
    message(FATAL_ERROR
      "Missing runtime artifact for ${flavor}:\n  ${_runtime_lib}\n"
      "Build it first from SDK GN/Ninja.")
  endif()
  foreach(_obj IN LISTS _runtime_extra_objects)
    if(NOT EXISTS "${_obj}")
      message(FATAL_ERROR "Missing required object for ${flavor}: ${_obj}")
    endif()
  endforeach()

  set(${out_runtime_lib} "${_runtime_lib}" PARENT_SCOPE)
  set(${out_extra_objs} "${_runtime_extra_objects}" PARENT_SCOPE)
endfunction()

function(_dartvm_embed_installable_path input_file out_install_rel)
  get_filename_component(_name "${input_file}" NAME)
  set(_install_rel "${CMAKE_INSTALL_DATADIR}/dartvm_embed_lib/runtime/${DARTSDK_BUILD_DIR}/${_name}")
  install(FILES "${input_file}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dartvm_embed_lib/runtime/${DARTSDK_BUILD_DIR}")
  set(${out_install_rel} "${_install_rel}" PARENT_SCOPE)
endfunction()

function(_dartvm_embed_add_variant flavor)
  _dartvm_embed_configure_variant(${flavor} _runtime_lib _runtime_extra_objects)

  _dartvm_embed_installable_path("${_runtime_lib}" _runtime_lib_install_rel)
  set(_runtime_lib_links
    "$<BUILD_INTERFACE:${_runtime_lib}>"
    "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${_runtime_lib_install_rel}>"
  )

  set(_runtime_extra_obj_links)
  foreach(_obj IN LISTS _runtime_extra_objects)
    _dartvm_embed_installable_path("${_obj}" _obj_install_rel)
    list(APPEND _runtime_extra_obj_links
      "$<BUILD_INTERFACE:${_obj}>"
      "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${_obj_install_rel}>"
    )
  endforeach()

  set(_target "dartvm_embed_lib_${flavor}")
  add_library(${_target} STATIC dartvm_embed_lib.cpp)

  target_include_directories(${_target}
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      "${DART_DIR}/runtime"
  )

  target_compile_definitions(${_target} PRIVATE
    DARTVM_EMBED_LIB_EXPORTING
    __STDC_LIMIT_MACROS
    __STDC_FORMAT_MACROS
    __STDC_CONSTANT_MACROS
  )

  if(NOT DARTVM_USE_STANDALONE_LIBDART)
    target_compile_definitions(${_target} PRIVATE
      DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP
    )
  endif()

  if(flavor STREQUAL "aot")
    target_compile_definitions(${_target} PRIVATE
      DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG
    )
  endif()

  if(UNIX AND NOT APPLE)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${_target} PRIVATE
      Threads::Threads
      ${CMAKE_DL_LIBS}
    )

    set(_clang_libcxx "${DART_DIR}/buildtools/linux-x64/clang/lib/x86_64-unknown-linux-gnu/libc++.a")
    set(_clang_libunwind "${DART_DIR}/buildtools/linux-x64/clang/lib/x86_64-unknown-linux-gnu/libunwind.a")
    if(NOT EXISTS "${_clang_libcxx}")
      message(FATAL_ERROR "Missing required runtime dep: ${_clang_libcxx}")
    endif()
    if(NOT EXISTS "${_clang_libunwind}")
      message(FATAL_ERROR "Missing required runtime dep: ${_clang_libunwind}")
    endif()

    _dartvm_embed_installable_path("${_clang_libcxx}" _clang_libcxx_install_rel)
    _dartvm_embed_installable_path("${_clang_libunwind}" _clang_libunwind_install_rel)

    target_link_libraries(${_target} PUBLIC
      "-Wl,--whole-archive"
      ${_runtime_lib_links}
      "-Wl,--no-whole-archive"
      "$<BUILD_INTERFACE:${_clang_libcxx}>"
      "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${_clang_libcxx_install_rel}>"
      "$<BUILD_INTERFACE:${_clang_libunwind}>"
      "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${_clang_libunwind_install_rel}>"
    )
  else()
    target_link_libraries(${_target} PUBLIC ${_runtime_lib_links})
  endif()

  if(_runtime_extra_obj_links)
    target_link_libraries(${_target} PUBLIC ${_runtime_extra_obj_links})
  endif()

  install(TARGETS ${_target}
    EXPORT dartvm_embed_libTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
endfunction()

_dartvm_embed_add_variant(jit)
_dartvm_embed_add_variant(aot)
