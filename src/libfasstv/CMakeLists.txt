add_library(fasstv SHARED
		SSTV.cpp
		SSTVEncode.cpp
		SSTVDecode.cpp
		${PROJECT_SOURCE_DIR}/src/util/Logger.cpp
		${PROJECT_SOURCE_DIR}/src/util/Rect.cpp
		${PROJECT_SOURCE_DIR}/src/util/StdoutSink.cpp
		${PROJECT_SOURCE_DIR}/src/util/ExportUtilities.cpp

		${PROJECT_SOURCE_DIR}/third_party/PicoSSTV/cordic.cpp
		${PROJECT_SOURCE_DIR}/third_party/PicoSSTV/half_band_filter2.cpp
		)

fasstv_setup_target(fasstv)

set_target_properties(fasstv PROPERTIES PUBLIC_HEADER ${PROJECT_SOURCE_DIR}/include/libfasstv/libfasstv.hpp)
target_include_directories(fasstv INTERFACE ${PROJECT_SOURCE_DIR}/include)

target_include_directories(fasstv PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(fasstv PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(fasstv
		debug SDL3::SDL3
		debug SDL3_image::SDL3_image
)
