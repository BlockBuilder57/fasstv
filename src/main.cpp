// Created by block on 11/12/24.

#include <util/Logger.hpp>
#include <util/StdoutSink.hpp>
#include <AudioExport.hpp>
#include <ImageUtilities.hpp>
#include <SSTV.hpp>

#include <cargs.h>

#include <fstream>
#include <filesystem>

static struct cag_option options[] = {
	{ .identifier = 'i', .access_letters = "i", .access_name = "input", .value_name = "<image file>", .description = "Path of input image" },
	{ .identifier = 'o', .access_letters = "o", .access_name = "output", .value_name = "<audio file>", .description = "Path of output audio. Defaults to the input path, with the mode appended, and with an mp3 extension." },
	{ .identifier = 'f', .access_letters = "f", .access_name = "format", .value_name = "<SSTV format|VIS code>", .description = "Specifies SSTV format by name or VIS code." },
	{ .identifier = 's', .access_letters = nullptr, .access_name = "stretch", .value_name = nullptr, .description = "Stretch to fit?" },
	{ .identifier = 'm', .access_letters = nullptr, .access_name = "method", .value_name = "<method>", .description = "Scale method (eg. bilinear, bicubic, nearest, etc.)" },
	{ .identifier = 'h', .access_letters = nullptr, .access_name = "help", .value_name = nullptr, .description = "Show help" }
};

struct ScaleMethod {
	std::string name;
	int flags;
};

static struct ScaleMethod ScaleMethods[] = {
	{"Bilinear", SWS_FAST_BILINEAR},
	{"Bicubic", SWS_BICUBIC},
	{"X", SWS_X},
	{"Nearest", SWS_POINT},
	{"Area", SWS_AREA},
	{"BicubicLinear", SWS_BICUBLIN},
	{"Gauss", SWS_GAUSS},
	{"Sinc", SWS_SINC},
	{"Lanczos", SWS_LANCZOS},
	{"Spline", SWS_SPLINE},
};

// https://stackoverflow.com/a/4119881
bool ichar_equals(char a, char b) {
	return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

int main(int argc, char** argv) {
	std::filesystem::path inputPath {};
	std::filesystem::path outputPath {};
	bool stretch = false;
	int resizeFlags = SWS_BICUBIC;

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
			case 's': {
				stretch = true;
				break;
			}
			case 'm': {
				const char* chary = cag_option_get_value(&context);
				if (chary != nullptr) {
					std::string str(chary);
					for (auto& sm : ScaleMethods) {
						if (std::ranges::equal(sm.name, str, ichar_equals))
							resizeFlags = sm.flags;
					}
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

	// fallback to Robot 36 if no mode set
	if (sstv.GetMode() == nullptr)
		sstv.SetMode("Robot 36");

	// load in image, get proper dimentions
	fasstv::SSTV::Mode* mode = sstv.GetMode();
	SDL_Surface* surfOrig = fasstv::LoadImage(inputPath);
	SDL_Rect letterbox = fasstv::CreateLetterbox(mode->width, mode->lines, {0, 0, surfOrig->w, surfOrig->h});

	SDL_Surface* surfOut;

	if (!stretch)
		surfOut = fasstv::RescaleImage(surfOrig, letterbox.w, letterbox.h, resizeFlags);
	else
		surfOut = fasstv::RescaleImage(surfOrig, mode->width, mode->lines, resizeFlags);

	SDL_free(surfOrig);

	// do the signal
	const int samplerate = 48000;
	sstv.SetSampleRate(samplerate);
	sstv.SetLetterboxLines(false);
	sstv.SetPixelProvider(&fasstv::GetSampleFromSurface);
	std::vector<float> samples = sstv.RunAllInstructions({0, 0, surfOut->w, surfOut->h});
	// turn down
	for (float& smp : samples)
		smp *= 0.6f;

	SDL_free(surfOut);

	std::string extension = ".wav";
	if (outputPath.empty()) {
		outputPath = inputPath;
		outputPath.replace_filename(inputPath.stem().string() + " " + sstv.GetMode()->name + extension);
	}

	std::ofstream file(outputPath.string(), std::ios::binary);
	fasstv::SamplesToWAV(samples, samplerate, file);
	file.close();

	return EXIT_SUCCESS;
}