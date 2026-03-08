cmake_minimum_required(VERSION 3.21)

add_executable(dartvm_embed_lib_unit_jit test_embed_api_jit.cpp)

target_include_directories(dartvm_embed_lib_unit_jit PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
)

target_compile_definitions(dartvm_embed_lib_unit_jit PRIVATE
  DARTVM_EMBED_LIB_UNIT_TEST_JIT=1
)

find_package(Threads REQUIRED)
target_link_libraries(dartvm_embed_lib_unit_jit PRIVATE
  dartvm_embed_lib_jit
  Threads::Threads
  ${CMAKE_DL_LIBS}
)

add_test(
  NAME dartvm_embed_lib_unit_jit
  COMMAND dartvm_embed_lib_unit_jit
)
