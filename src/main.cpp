// Created by block on 11/12/24.

#include <util/Logger.hpp>
#include <util/StdoutSink.hpp>

int main(int argc, char** argv) {
	static_cast<void>(argc);
	static_cast<void>(argv);

	fasstv::LoggerAttachStdout();

	fasstv::Logger::The().Debug("Built {} {}", __DATE__, __TIME__);

	return 0;
}