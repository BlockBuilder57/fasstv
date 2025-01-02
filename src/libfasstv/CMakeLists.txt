add_library(fasstv SHARED
		SSTV.cpp
		SSTVEncode.cpp
		ExportUtils.cpp
		${PROJECT_SOURCE_DIR}/src/util/Logger.cpp
		${PROJECT_SOURCE_DIR}/src/util/Rect.cpp
		${PROJECT_SOURCE_DIR}/src/util/StdoutSink.cpp
		)

fasstv_setup_target(fasstv)

set_target_properties(fasstv PROPERTIES PUBLIC_HEADER ${PROJECT_SOURCE_DIR}/include/libfasstv/libfasstv.hpp)
target_include_directories(fasstv INTERFACE ${PROJECT_SOURCE_DIR}/include)

target_include_directories(fasstv PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(fasstv PRIVATE ${PROJECT_SOURCE_DIR}/src)