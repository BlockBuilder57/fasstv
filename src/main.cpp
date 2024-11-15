// Created by block on 11/12/24.

#include <util/Logger.hpp>
#include <util/StdoutSink.hpp>
#include <AudioExport.hpp>
#include <SSTV.hpp>

#include <cargs.h>
#include <SDL3_image/SDL_image.h>

#include <fstream>
#include <filesystem>

extern "C" {
	#include <libavcodec/avcodec.h>

	#include <libavutil/samplefmt.h>
	#include <libavutil/imgutils.h>

	#include <libswscale/swscale.h>
}

static struct cag_option options[] = {
	{ .identifier = 'i', .access_letters = "i", .access_name = "input", .value_name = "<image file>", .description = "Path of input image" },
	{ .identifier = 'o', .access_letters = "o", .access_name = "output", .value_name = nullptr, .description = "Path of output audio. Defaults to the input path, with the mode appended, and with an mp3 extension." },
	{ .identifier = 'f', .access_letters = "f", .access_name = "format", .value_name = "<SSTV format|VIS code>", .description = "Specifies SSTV format by name or VIS code." },
	{ .identifier = 'h', .access_letters = nullptr, .access_name = "help", .value_name = nullptr, .description = "Show help" }
};

SDL_Surface* surfOrig = nullptr, *surfOut = nullptr;
std::uint8_t colorHolder[4] = {};
std::uint32_t defaultColor = 0x88888888;

std::uint8_t* GetSampleFromSurface(int sample_x, int sample_y) {
	if (surfOut == nullptr)
		return reinterpret_cast<std::uint8_t*>(&defaultColor);

	SDL_ReadSurfacePixel(surfOut, sample_x, surfOut->h - sample_y, &colorHolder[0], &colorHolder[1], &colorHolder[2], &colorHolder[3]);
	return &colorHolder[0];
}

int main(int argc, char** argv) {
	std::filesystem::path inputPath {};
	std::filesystem::path outputPath {};

	fasstv::LoggerAttachStdout();
	fasstv::LogDebug("Built {} {}", __DATE__, __TIME__);

	fasstv::SSTV& sstv = fasstv::SSTV::The();

	cag_option_context context;
	cag_option_init(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
	while (cag_option_fetch(&context)) {
		switch (cag_option_get_identifier(&context)) {
			case 'i': {
				const char* chary = cag_option_get_value(&context);
				if (chary != nullptr)
					inputPath = std::filesystem::path(chary);
				break;
			}
			case 'o': {
				const char* chary = cag_option_get_value(&context);
				if (chary != nullptr)
					outputPath = std::filesystem::path(chary);
				break;
			}
			case 'f': {
				const char* chary = cag_option_get_value(&context);
				if (chary != nullptr) {
					if (isdigit(chary[0]))
						sstv.SetMode(std::atoi(chary));
					else
						sstv.SetMode(chary);
				}
				break;
			}
			case 'h':
				printf("Usage: fasstv [OPTIONS]...\n");
				printf("Quickly converts an image file into a valid SSTV signal.\n\n");
				cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
				return EXIT_SUCCESS;
			case '?':
				cag_option_print_error(&context, stdout);
				break;
		}
	}

	for (int param_index = cag_option_get_index(&context); param_index < argc; ++param_index) {
		//printf("additional parameter: %s\n", argv[param_index]);
	}

	if (inputPath.empty()) {
		fasstv::LogError("Need a file to load!");
		return 1;
	}

	surfOrig = IMG_Load(inputPath.c_str());
	if (!surfOrig) {
		fasstv::LogError("SDL3_image failed to load texture! {}", inputPath.c_str());
		return 1;
	}

	// fallback to Robot 36 if no mode set
	if (sstv.GetMode() == nullptr)
		sstv.SetMode("Robot 36");

	fasstv::SSTV::Mode* mode = sstv.GetMode();
	surfOut = SDL_CreateSurface(mode->width, mode->lines, SDL_PIXELFORMAT_RGBA32);

	// convert orig to RGBA32 and make sure to free (since we need to do some pointer juggling)
	SDL_Surface* surfTemp = SDL_ConvertSurface(surfOrig, SDL_PIXELFORMAT_RGBA32);
	SDL_free(surfOrig);
	surfOrig = surfTemp;

	SwsContext* sws_ctx = sws_getContext(
		surfOrig->w, surfOrig->h, AV_PIX_FMT_RGBA,
		surfOut->w, surfOut->h, AV_PIX_FMT_RGBA,
		SWS_BICUBIC, NULL, NULL, NULL
	);

	int src_linesize[4], dst_linesize[4];

	// we don't need to make new buffers, let's just get linesizes
	av_image_fill_linesizes(&src_linesize[0], AV_PIX_FMT_RGBA, surfOrig->w);
	av_image_fill_linesizes(&dst_linesize[0], AV_PIX_FMT_RGBA, surfOut->w);

	// do the scale? wow this works hahaha
	sws_scale(sws_ctx, reinterpret_cast<const uint8_t* const*>(&(surfOrig->pixels)),
			  src_linesize, 0, surfOrig->h, reinterpret_cast<uint8_t* const*>(&(surfOut->pixels)), dst_linesize);

	sws_freeContext(sws_ctx);

	sstv.SetPixelProvider(&GetSampleFromSurface);
	auto samples = sstv.DoTheThing({0, 0, surfOut->w, surfOut->h});
	for (float& smp : samples)
		smp *= 0.6f;

	std::string extension = ".wav";

	if (outputPath.empty()) {
		outputPath = inputPath;
		outputPath.replace_filename(inputPath.stem().string() + " " + sstv.GetMode()->name + extension);
	}

	std::ofstream file(outputPath.string(), std::ios::binary);
	fasstv::SamplesToWAV(samples, 44100, file);
	file.close();

	return EXIT_SUCCESS;
}