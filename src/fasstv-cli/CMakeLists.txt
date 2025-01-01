add_executable(fasstv-cli
		main.cpp
		${CMAKE_SOURCE_DIR}/src/util/Logger.cpp
		${CMAKE_SOURCE_DIR}/src/util/Rect.cpp
		${CMAKE_SOURCE_DIR}/src/util/StdoutSink.cpp
		${CMAKE_SOURCE_DIR}/src/util/AudioExport.cpp
		${CMAKE_SOURCE_DIR}/src/util/ImageUtilities.cpp
		)

fasstv_setup_target(fasstv-cli)

if(FFMPEG_FOUND)
	#  FFMPEG_INCLUDE_DIRS  - Include directory necessary for using the required components headers.
	#  FFMPEG_LIBRARIES     - Link these to use the required ffmpeg components.
	#  FFMPEG_DEFINITIONS   - Compiler switches required for using the required ffmpeg components.
	message("FFMPEG_INCLUDE_DIRS = ${FFMPEG_INCLUDE_DIRS} ")
	message("FFMPEG_LIBRARIES = ${FFMPEG_LIBRARIES} ")
	message("FFMPEG_DEFINITIONS = ${FFMPEG_DEFINITIONS} ")

	target_include_directories(fasstv-cli PRIVATE ${FFMPEG_INCLUDE_DIRS})

endif()

target_include_directories(fasstv-cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(fasstv-cli PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(fasstv-cli
		fasstv
		SDL3::SDL3
		SDL3_image::SDL3_image
		cargs
		fftw3

		${FFMPEG_LIBRARIES}
		)
