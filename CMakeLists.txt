cmake_minimum_required(VERSION 3.21)

project(DARTVM_EMBED_LIB VERSION 0.1.0)
include(GNUInstallDirs)
include(CTest)

set(DARTSDK_ROOTDIR "${PROJECT_SOURCE_DIR}/dart-sdk")
set(DARTSDK_BUILD_DIR "ReleaseX64")

option(DARTVM_USE_STANDALONE_LIBDART OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(DARTVM_EMBED_LIB_DIR "${PROJECT_SOURCE_DIR}/src")
set(DART_DIR "${DARTSDK_ROOTDIR}/sdk")

add_subdirectory(src)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
