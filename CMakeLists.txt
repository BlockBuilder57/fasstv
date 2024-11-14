cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG")

project(fasstv
	LANGUAGES C CXX
	)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

#include(Policies)
include(ProjectFuncs)

find_package(FFmpeg COMPONENTS AVCODEC AVFORMAT AVUTIL AVDEVICE REQUIRED)
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

# default linker
if(NOT FASSTV_LINKER AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(FASSTV_LINKER "lld")
elseif(NOT FASSTV_LINKER)
	set(FASSTV_LINKER "bfd")
endif()

fasstv_set_alternate_linker()

add_subdirectory(src)

