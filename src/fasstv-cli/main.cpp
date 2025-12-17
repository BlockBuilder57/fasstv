// Created by block on 11/12/24.

#include <SDL3/SDL.h>

#include <fasstv-cli/Options.hpp>
#include <fasstv-cli/Processes.hpp>

#include <libfasstv/libfasstv.hpp>

#include <shared/ExportUtilities.hpp>
#include <shared/ImageUtilities.hpp>
#include <shared/Logger.hpp>
#include <shared/StdoutSink.hpp>

#include "gitversion.h"

int main(int argc, char** argv) {
	fasstv::LoggerAttachStdout();

	int ret = fasstv::cli::Options::ParseArgs(argc, argv);
	if (ret != EXIT_SUCCESS)
		return ret;

	fasstv::LogDebug("fasstv-cli {}", fasstv::version::fullTag);
	fasstv::LogDebug("Built {} {}", __DATE__, __TIME__);
	fasstv::LogDebug("SDL {}, rev {}", SDL_VERSION, SDL_GetRevision());

#ifdef FASSTV_DEBUG
	fasstv::cli::Options::PrintArgs();
#endif

	fasstv::SSTVMetadata::BuildMetadata();

	switch (fasstv::cli::Options::options.fasstv_mode) {
		case fasstv::cli::FASSTVMode::Encode:
			ret = fasstv::cli::Processes::The().ProcessEncode();
			break;
		case fasstv::cli::FASSTVMode::Decode:
			ret = fasstv::cli::Processes::The().ProcessDecode();
			break;
		case fasstv::cli::FASSTVMode::Transcode:
			ret = fasstv::cli::Processes::The().ProcessTranscode();
			break;
		default:
			fasstv::LogError("Invalid fasstv mode... how did you do that?");
			break;
	}

	if (ret != EXIT_SUCCESS)
		return ret;

	return EXIT_SUCCESS;
}