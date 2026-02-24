include(CMakeParseArguments)

function(dartvm_embed_add_program_target target_name)
  set(options)
  set(one_value_args DART_FILE OUTPUT PUBSPEC_DIR WORKING_DIRECTORY FLAVOR)
  set(multi_value_args EXTRA_INPUTS)
  cmake_parse_arguments(DVE "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT DVE_DART_FILE)
    message(FATAL_ERROR "dartvm_embed_add_program_target requires DART_FILE")
  endif()

  if(NOT DVE_OUTPUT)
    set(DVE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/program.bin")
  endif()

  if(NOT DVE_WORKING_DIRECTORY)
    set(DVE_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  if(NOT DVE_FLAVOR)
    set(DVE_FLAVOR "${DARTVM_EMBED_DEFAULT_FLAVOR}")
  endif()
  if(NOT DVE_FLAVOR STREQUAL "jit" AND NOT DVE_FLAVOR STREQUAL "aot")
    message(FATAL_ERROR "dartvm_embed_add_program_target: FLAVOR must be jit|aot")
  endif()

  if(NOT EXISTS "${DARTVM_EMBED_DART_BIN}")
    message(FATAL_ERROR "DARTVM_EMBED_DART_BIN not found: ${DARTVM_EMBED_DART_BIN}")
  endif()

  set(_deps "${DVE_DART_FILE}" ${DVE_EXTRA_INPUTS})

  if(DVE_PUBSPEC_DIR)
    set(_pubspec_yaml "${DVE_PUBSPEC_DIR}/pubspec.yaml")
    set(_pubspec_lock "${DVE_PUBSPEC_DIR}/pubspec.lock")
    set(_package_config "${DVE_PUBSPEC_DIR}/.dart_tool/package_config.json")

    if(NOT EXISTS "${_pubspec_yaml}")
      message(FATAL_ERROR "pubspec.yaml not found: ${_pubspec_yaml}")
    endif()

    add_custom_command(
      OUTPUT "${_package_config}" "${_pubspec_lock}"
      COMMAND "${DARTVM_EMBED_DART_BIN}" pub get
      WORKING_DIRECTORY "${DVE_PUBSPEC_DIR}"
      DEPENDS "${_pubspec_yaml}"
      COMMENT "Running dart pub get (${target_name})"
      VERBATIM
    )

    list(APPEND _deps "${_package_config}")
  endif()

  if(DVE_FLAVOR STREQUAL "aot")
    if(NOT EXISTS "${DARTVM_EMBED_GEN_SNAPSHOT_BIN}")
      message(FATAL_ERROR "gen_snapshot not found: ${DARTVM_EMBED_GEN_SNAPSHOT_BIN}")
    endif()
    if(NOT EXISTS "${DARTVM_EMBED_GEN_KERNEL_DART}")
      message(FATAL_ERROR "gen_kernel.dart not found: ${DARTVM_EMBED_GEN_KERNEL_DART}")
    endif()
    if(NOT EXISTS "${DARTVM_EMBED_VM_PLATFORM_DILL}")
      message(FATAL_ERROR "vm platform dill not found: ${DARTVM_EMBED_VM_PLATFORM_DILL}")
    endif()

    set(_kernel_out "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_kernel_aot.dill")

    add_custom_command(
      OUTPUT "${_kernel_out}"
      COMMAND "${DARTVM_EMBED_DART_BIN}" "${DARTVM_EMBED_GEN_KERNEL_DART}"
              --platform "${DARTVM_EMBED_VM_PLATFORM_DILL}"
              --aot
              --tfa
              "${DVE_DART_FILE}"
              -o "${_kernel_out}"
      WORKING_DIRECTORY "${DVE_WORKING_DIRECTORY}"
      DEPENDS ${_deps}
      COMMENT "Compiling Dart AOT kernel (${target_name})"
      VERBATIM
    )

    add_custom_command(
      OUTPUT "${DVE_OUTPUT}"
      COMMAND "${DARTVM_EMBED_GEN_SNAPSHOT_BIN}"
              --snapshot_kind=app-aot-elf
              --elf=${DVE_OUTPUT}
              "${_kernel_out}"
      WORKING_DIRECTORY "${DVE_WORKING_DIRECTORY}"
      DEPENDS "${_kernel_out}"
      COMMENT "Generating app-aot-elf snapshot (${target_name})"
      VERBATIM
    )
  else()
    add_custom_command(
      OUTPUT "${DVE_OUTPUT}"
      COMMAND "${DARTVM_EMBED_DART_BIN}" compile kernel
              "${DVE_DART_FILE}"
              -o "${DVE_OUTPUT}"
      WORKING_DIRECTORY "${DVE_WORKING_DIRECTORY}"
      DEPENDS ${_deps}
      COMMENT "Compiling Dart kernel (${target_name})"
      VERBATIM
    )
  endif()

  add_custom_target(${target_name} DEPENDS "${DVE_OUTPUT}")
  set_property(TARGET ${target_name} PROPERTY DARTVM_EMBED_PROGRAM_PATH "${DVE_OUTPUT}")
endfunction()
