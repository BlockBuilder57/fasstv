add_executable(fasstv
		main.cpp
		util/Logger.cpp
		util/StdoutSink.cpp
		util/Rect.cpp
		util/Oscillator.cpp
		SSTV.cpp
		)

fasstv_setup_target(fasstv)

target_link_libraries(fasstv)
