cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG")

project(fasstv
	LANGUAGES C CXX
	)
project(fasstv-cli
	LANGUAGES C CXX
	)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

#include(Policies)
include(cmake/ProjectFuncs.cmake)

find_package(Python3 REQUIRED
		COMPONENTS Interpreter)

find_package(FFmpeg COMPONENTS AVCODEC AVFORMAT AVUTIL AVDEVICE SWSCALE REQUIRED)
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

include(FetchContent)
FetchContent_Declare(
		argparse
		GIT_REPOSITORY https://github.com/p-ranav/argparse.git
)
FetchContent_MakeAvailable(argparse)

# default linker
if(NOT FASSTV_LINKER AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(FASSTV_LINKER "lld")
elseif(NOT FASSTV_LINKER)
	set(FASSTV_LINKER "bfd")
endif()

fasstv_set_alternate_linker()

set(VERSION_FILENAME gitversion.h)

add_custom_target(__fasstv_gittag
		COMMAND $<TARGET_FILE:Python3::Interpreter> ${PROJECT_SOURCE_DIR}/scripts/gittag.py ${PROJECT_BINARY_DIR}/${VERSION_FILENAME}
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		SOURCES ${PROJECT_BINARY_DIR}/${VERSION_FILENAME}
)
set_source_files_properties(${PROJECT_BINARY_DIR}/${VERSION_FILENAME} PROPERTIES GENERATED TRUE)

add_subdirectory(src/libfasstv)
add_subdirectory(src/fasstv-cli)