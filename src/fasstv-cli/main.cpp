// Created by block on 11/12/24.

#include <util/Logger.hpp>
#include <util/Rect.hpp>
#include <util/StdoutSink.hpp>
#include <util/AudioExport.hpp>
#include <util/ImageUtilities.hpp>
#include <libfasstv/SSTVEncode.hpp>
#include <libfasstv/ExportUtils.hpp>

#include <cargs.h>
#include <fftw3.h>
#include <SDL3/SDL.h>

#include <complex>
#include <fstream>
#include <filesystem>

static struct cag_option options[] = {
	{ .identifier = 'i', .access_letters = "i", .access_name = "input", .value_name = "<image file>", .description = "Path of the input image." },
	{ .identifier = 'o', .access_letters = "o", .access_name = "output", .value_name = "<audio file>", .description = "Path of the output audio. Defaults to the input path, with the mode appended, and with an mp3 extension." },
	{ .identifier = 'f', .access_letters = "f", .access_name = "format", .value_name = "<SSTV format|VIS code>", .description = "Specifies SSTV format by name or VIS code." },
	{ .identifier = 's', .access_letters = nullptr, .access_name = "stretch", .value_name = nullptr, .description = "If specified, stretch to fit." },
	{ .identifier = 'm', .access_letters = "m", .access_name = "method", .value_name = "<method>", .description = "Scale method (eg. bilinear, bicubic, nearest, etc.)" },
	{ .identifier = 'p', .access_letters = nullptr, .access_name = "play", .value_name = nullptr, .description = "Whether audio should be played from the executable." },
	{ .identifier = 'c', .access_letters = "c", .access_name = "separate-scans", .value_name = nullptr, .description = "Outputs two audio files, one containing sync pulses and the other scanlines." },
	{ .identifier = 'v', .access_letters = "v", .access_name = "volume", .value_name = "<0.0-1.0>", .description = "Volume of audio files. Default of 0.15." },
	{ .identifier = 'h', .access_letters = "h", .access_name = "help", .value_name = nullptr, .description = "Show help" }
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
	{"Point", SWS_POINT},
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

void OutputSamples(fasstv::SSTVEncode& sstvenc, SDL_Surface* surfOut, std::filesystem::path& outputPath, int samplerate, float volume) {
	if (outputPath.empty())
		return;

	// one-shot
	std::vector<float> samples;
	sstvenc.RunAllInstructions(samples, {0, 0, surfOut->w, surfOut->h});
	// turn down
	for (float& smp : samples)
		smp *= volume;

	// for automatic file naming
	/*std::string extension = ".wav";
	if (outputPath.empty()) {
		outputPath = inputPath;
		outputPath.replace_filename(inputPath.filename().string() + " " + sstv.GetMode()->name + extension);
	}*/

	fasstv::LogInfo("Saving {}...", outputPath.filename().c_str());
	std::ofstream file(outputPath.string(), std::ios::binary);
	if (outputPath.extension() == ".wav")
		fasstv::SamplesToWAV(samples, samplerate, file);
	else
		fasstv::SamplesToAVCodec(samples, samplerate, file);
	file.close();
	samples.clear();
}

int main(int argc, char** argv) {
	std::filesystem::path inputPath {};
	std::filesystem::path outputPath {};
	bool stretch = false;
	bool play = false;
	bool separateScans = false;
	int resizeFlags = SWS_BICUBIC;
	float volume = 0.15f;

	fasstv::LoggerAttachStdout();
	fasstv::LogDebug("Built {} {}", __DATE__, __TIME__);

	fasstv::LogDebug("{}", SDL_VERSION);
	fasstv::LogDebug("{}", SDL_GetRevision());

	if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO /*| SDL_INIT_CAMERA*/)) {
		fasstv::LogError("Couldn't initialize SDL: {}", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	fasstv::SSTVEncode& sstvenc = fasstv::SSTVEncode::The();

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
						sstvenc.SetMode(std::atoi(chary));
					else
						sstvenc.SetMode(chary);
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
			case 'p': {
				play = true;
				break;
			}
			case 'c': {
				separateScans = true;
				break;
			}
			case 'v': {
				volume = atof(cag_option_get_value(&context));
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

	/*const int windowDimensions[2] = {1024, 512};
	SDL_Renderer* renderer;
	SDL_Window* window;
	SDL_CreateWindowAndRenderer("fasstv", windowDimensions[0], windowDimensions[1], 0, &window, &renderer);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);*/

	SDL_AudioStream* stream = nullptr;

	if (play) {
		SDL_AudioSpec spec {
			.format = SDL_AUDIO_F32,
			.channels = 1,
			.freq = 8000
		};

		stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
		if (!stream) {
			fasstv::LogError("Couldn't create audio stream: {}", SDL_GetError());
			return SDL_APP_FAILURE;
		}

		fasstv::LogInfo("Trying to play through {}...", SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream)));

		SDL_ResumeAudioStreamDevice(stream);
	}

	/*int devcount = 0;
	SDL_CameraID* devices = SDL_GetCameras(&devcount);
	if (devices == NULL) {
		fasstv::LogError("Couldn't enumerate camera devices: {}", SDL_GetError());
		return SDL_APP_FAILURE;
	} else if (devcount == 0) {
		fasstv::LogError("Couldn't find any camera devices! Please connect a camera and try again.");
		return SDL_APP_FAILURE;
	}

	fasstv::LogDebug("Found {} cameras", devcount);
	for (int i = 0; i < devcount; i++)
		fasstv::LogDebug("Camera {}: {}", devices[i], SDL_GetCameraName(devices[i]));
	SDL_CameraID camId = devices[1];

	int formatCount = 0;
	SDL_CameraSpec desiredSpec = {};
	SDL_CameraSpec** camFormats = SDL_GetCameraSupportedFormats(camId, &formatCount);
	for (int i = 0; i < formatCount; i++) {
		SDL_CameraSpec* pSpec = camFormats[i];
		SDL_Log("Cam %d Mode %d: %dx%d @ %d/%d - %s", camId, i, pSpec->width, pSpec->height, pSpec->framerate_numerator, pSpec->framerate_denominator, SDL_GetPixelFormatName(pSpec->format));
		if (pSpec->framerate_numerator == 30) {
			SDL_Log("Trying this one");
			memcpy(&desiredSpec, pSpec, sizeof(SDL_CameraSpec));
			break;
		}

	}

	SDL_Camera* cam = SDL_OpenCamera(camId, nullptr);
	SDL_free(devices);*/

	// fallback to Robot 36 if no mode set
	if (sstvenc.GetMode() == nullptr)
		sstvenc.SetMode("Robot 36");

	// load in image, get proper dimensions
	fasstv::SSTV::Mode* mode = sstvenc.GetMode();
	SDL_Surface* surfOrig; //= SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_RGBA32);

	surfOrig = fasstv::LoadImage(inputPath);
	if (surfOrig == nullptr)
		return EXIT_FAILURE;

	//SDL_free(surfOrig);
	/*surfOrig = nullptr;
	Uint64 timestampNS;
	while (surfOrig == nullptr)
		surfOrig = SDL_AcquireCameraFrame(cam, &timestampNS);*/

	fasstv::Rect letterbox = fasstv::Rect::CreateLetterbox(mode->width, mode->lines, {0, 0, surfOrig->w, surfOrig->h});

	SDL_Surface* surfOut;

	if (!stretch)
		surfOut = fasstv::RescaleImage(surfOrig, letterbox.w, letterbox.h, resizeFlags);
	else
		surfOut = fasstv::RescaleImage(surfOrig, mode->width, mode->lines, resizeFlags);

	SDL_free(surfOrig);

	// do the signal
	const int samplerate = 8000;
	sstvenc.SetSampleRate(samplerate);
	sstvenc.SetLetterbox(fasstv::Rect::CreateLetterbox(mode->width, mode->lines, {0, 0, surfOut->w, surfOut->h}));
	sstvenc.SetLetterboxLines(false);
	sstvenc.SetPixelProvider(&fasstv::GetSampleFromSurface);

	if (separateScans) {
		std::filesystem::path outScan = outputPath.parent_path() += (outputPath.stem().string() + "-scan" + outputPath.extension().string());
		std::filesystem::path outSync = outputPath.parent_path() += (outputPath.stem().string() + "-sync" + outputPath.extension().string());

		sstvenc.SetInstructionFlagMask(fasstv::SSTV::InstructionFlags::PitchIsDelegated, true);
		OutputSamples(sstvenc, surfOut, outScan, samplerate, volume);
		sstvenc.SetInstructionFlagMask(fasstv::SSTV::InstructionFlags::PitchIsDelegated, false);
		OutputSamples(sstvenc, surfOut, outSync, samplerate, volume);
		sstvenc.SetInstructionFlagMask((fasstv::SSTV::InstructionFlags)0, false);
	}
	else {
		OutputSamples(sstvenc, surfOut, outputPath, samplerate, volume);
	}

	/*SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
	const int reflines[4] = {1100, 2300, 5, 5};
	for (int num : reflines) {
		for (int i = 0; i < 1024; i+=2)
			SDL_RenderPoint(renderer, i, windowDimensions[1]-(num/8.f));
	}

	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	SDL_RenderPresent(renderer);

	const int slideyLen = 128;
	int line = 0;
	const int lineSpread = 1;
	const float binSize = samplerate/slideyLen;

	fftw_complex* out;
	fftw_plan p;

	std::array<double, slideyLen> doubleSamples {};

	out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * slideyLen);
	p = fftw_plan_dft_r2c_1d(slideyLen, doubleSamples.data(), out, FFTW_ESTIMATE);

	for (int i = 0; i < samples.size(); i += slideyLen) {
		for (int j = 0; j < slideyLen; j++)
			doubleSamples[j] = samples[std::min(i+j, (int)samples.size())];

		fftw_execute(p);

		float bestMag = -1;
		int bestMaxIdx = -1;

		for (int j = 0; j < slideyLen/2; j++) {
			auto thing = reinterpret_cast<std::complex<double>*>(out[j]);
			if (thing->imag() >= bestMag) {
				bestMag = thing->imag();
				bestMaxIdx = j;
			}
		}

		bestMag = 255.f;
		SDL_SetRenderDrawColor(renderer, bestMag, bestMag, bestMag, 255);

		for (int k = 0; k < lineSpread; k++)
			SDL_RenderPoint(renderer, line+k, windowDimensions[1]-(bestMaxIdx * binSize / 8.f));

		line += lineSpread;
		SDL_RenderPresent(renderer);
	}

	fftw_destroy_plan(p);
	fftw_free(out);*/

	const size_t buff_size = 320;
	static float buff[buff_size];
	sstvenc.ResetInstructionProcessing();

	SDL_Event event;
	bool run = true;

	//SDL_Surface *surfFrame = nullptr, *surfOut = nullptr;
	//Uint64 timestampNS;

	while (run) {
		SDL_PollEvent(&event);
		switch (event.type) {
			case SDL_EVENT_QUIT:
				run = false;
				break;
		}

		/*SDL_Surface* surfTemp = SDL_AcquireCameraFrame(cam, &timestampNS);
		if (surfTemp) {
			fasstv::LogDebug("Got a new frame?");
			surfFrame = surfTemp;
		}

		if (surfFrame) {
			fasstv::Rect letterbox = fasstv::Rect::CreateLetterbox(mode->width, mode->lines, {0, 0, surfFrame->w, surfFrame->h});

			if (surfOut) {
				SDL_free(surfOut);
				surfOut = nullptr;
			}

			if (!stretch)
				surfOut = fasstv::RescaleImage(surfFrame, letterbox.w, letterbox.h, resizeFlags);
			else
				surfOut = fasstv::RescaleImage(surfFrame, mode->width, mode->lines, resizeFlags);

			SDL_free(surfFrame);
		}*/

		if (stream != nullptr) {
			const int minimum_audio = samplerate;
			if (SDL_GetAudioStreamAvailable(stream) < minimum_audio) {
				if (!sstvenc.IsProcessingDone() && surfOut != nullptr) {
					sstvenc.PumpInstructionProcessing(&buff[0], buff_size, {0, 0, surfOut->w, surfOut->h});
					for (float& smp : buff)
						smp *= volume;
					SDL_PutAudioStreamData(stream, buff, sizeof (buff));

					/*std::int32_t cur_x, cur_y;
					std::uint32_t cur_sample, length_in_samples;
					sstvenc.GetState(&cur_x, &cur_y, &cur_sample, &length_in_samples);
					fasstv::LogDebug("Progress: {:.2f}%", (cur_sample / (float)length_in_samples) * 100.f);*/
				}
				else {
					run = false;
				}
			}
		}
		else {
			run = false;
		}
	}

	SDL_free(surfOut);

	SDL_Quit();
	return EXIT_SUCCESS;
}