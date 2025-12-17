add_executable(fasstv-cli
		main.cpp
		Options.cpp
		Processes.cpp

		${PROJECT_SOURCE_DIR}/src/shared/Logger.cpp
		${PROJECT_SOURCE_DIR}/src/shared/Rect.cpp
		${PROJECT_SOURCE_DIR}/src/shared/StdoutSink.cpp
		${PROJECT_SOURCE_DIR}/src/shared/ExportUtilities.cpp
		${PROJECT_SOURCE_DIR}/src/shared/ImageUtilities.cpp
		)

fasstv_setup_target(fasstv-cli)

add_dependencies(fasstv-cli __fasstv_gittag)

if(FFMPEG_FOUND)
	#  FFMPEG_INCLUDE_DIRS  - Include directory necessary for using the required components headers.
	#  FFMPEG_LIBRARIES     - Link these to use the required ffmpeg components.
	#  FFMPEG_DEFINITIONS   - Compiler switches required for using the required ffmpeg components.
	message("FFMPEG_INCLUDE_DIRS = ${FFMPEG_INCLUDE_DIRS} ")
	message("FFMPEG_LIBRARIES = ${FFMPEG_LIBRARIES} ")
	message("FFMPEG_DEFINITIONS = ${FFMPEG_DEFINITIONS} ")

	target_include_directories(fasstv-cli PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()

target_include_directories(fasstv-cli PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(fasstv-cli PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(fasstv-cli PRIVATE ${PROJECT_BINARY_DIR}/)

target_link_libraries(fasstv-cli
		fasstv
		SDL3::SDL3
		SDL3_image::SDL3_image
		argparse

		${FFMPEG_LIBRARIES}
		)
