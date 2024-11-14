add_executable(fasstv
		main.cpp
		util/Logger.cpp
		util/StdoutSink.cpp
		util/Oscillator.cpp
		SSTV.cpp
		)

fasstv_setup_target(fasstv)

if(FFMPEG_FOUND)
	#  FFMPEG_INCLUDE_DIRS  - Include directory necessary for using the required components headers.
	#  FFMPEG_LIBRARIES     - Link these to use the required ffmpeg components.
	#  FFMPEG_DEFINITIONS   - Compiler switches required for using the required ffmpeg components.
	message("FFMPEG_INCLUDE_DIRS = ${FFMPEG_INCLUDE_DIRS} ")
	message("FFMPEG_LIBRARIES = ${FFMPEG_LIBRARIES} ")
	message("FFMPEG_DEFINITIONS = ${FFMPEG_DEFINITIONS} ")

	target_include_directories(fasstv PRIVATE ${FFMPEG_INCLUDE_DIRS})

endif()

target_link_libraries(fasstv
		SDL3::SDL3
		SDL3_image::SDL3_image

		${FFMPEG_LIBRARIES}
		)
