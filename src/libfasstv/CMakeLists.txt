add_library(fasstv SHARED
		SSTV.cpp
		SSTVEncode.cpp
		${CMAKE_SOURCE_DIR}/src/util/Logger.cpp
		${CMAKE_SOURCE_DIR}/src/util/Rect.cpp
		${CMAKE_SOURCE_DIR}/src/util/StdoutSink.cpp
		)

fasstv_setup_target(fasstv)

set_target_properties(fasstv PROPERTIES PUBLIC_HEADER ${CMAKE_SOURCE_DIR}/include/libfasstv/libfasstv.hpp)

target_include_directories(fasstv PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(fasstv PRIVATE ${CMAKE_SOURCE_DIR}/src)