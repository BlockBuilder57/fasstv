// Created by block on 11/12/24.

#include <cargs.h>
#include <fftw3.h>
#include <SDL3/SDL.h>

#include <libfasstv/libfasstv.hpp>
#include <util/ExportUtilities.hpp>
#include <util/ImageUtilities.hpp>
#include <util/Logger.hpp>
#include <util/Rect.hpp>
#include <util/StdoutSink.hpp>

static struct cag_option options[] = {
	{ .identifier = 'i', .access_letters = "i", .access_name = "input", .value_name = "<image file>", .description = "Path of the input image." },
	{ .identifier = 'o', .access_letters = "o", .access_name = "output", .value_name = "<audio file>", .description = "Path of the output audio. Defaults to the input path, with the mode appended, and with an mp3 extension." },
	{ .identifier = 'd', .access_letters = "d", .access_name = "decode", .value_name = nullptr, .description = "Whether we should immediately decode our signal. Will be moved elsewhere." },
	{ .identifier = 'f', .access_letters = "f", .access_name = "format", .value_name = "<SSTV format|VIS code>", .description = "Specifies SSTV format by name or VIS code." },
	{ .identifier = 's', .access_letters = nullptr, .access_name = "stretch", .value_name = nullptr, .description = "If specified, stretch to fit." },
	{ .identifier = 'm', .access_letters = "m", .access_name = "method", .value_name = "<method>", .description = "Scale method (eg. bilinear, bicubic, nearest, etc.)" },
	{ .identifier = 'p', .access_letters = nullptr, .access_name = "play", .value_name = nullptr, .description = "Whether audio should be played from the executable." },
	{ .identifier = 'c', .access_letters = "c", .access_name = "separate-scans", .value_name = nullptr, .description = "Outputs two audio files, one containing sync pulses and the other scanlines." },
	{ .identifier = 'v', .access_letters = "v", .access_name = "volume", .value_name = "<0.0-1.0>", .description = "Volume of audio files. Default of 0.15." },
	{ .identifier = 'r', .access_letters = "r", .access_name = "samplerate", .value_name = "<rate in Hertz>", .description = "Sample rate of signals/audio files. Default of 8000." },
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

void OutputSamples(fasstv::SSTVEncode& sstvenc, SDL_Surface* image, std::filesystem::path& outputPath, int samplerate, float volume) {
	if (outputPath.empty())
		return;

	// one-shot
	std::vector<float> samples;
	sstvenc.RunAllInstructions(samples, {0, 0, image->w, image->h});
	// turn down
	for (float& smp : samples)
		smp *= volume;

	// for automatic file naming
	if (!outputPath.has_extension()) {
		outputPath.replace_extension(".wav");
	}

	fasstv::LogInfo("Saving {}...", outputPath.c_str());
	std::ofstream file(outputPath.string(), std::ios::binary);
	if (outputPath.extension() == ".wav")
		fasstv::SamplesToWAV(samples, samplerate, file);
	else
		fasstv::SamplesToAVCodec(samples, samplerate, file);
	file.close();
	samples.clear();
}

void OutputImage(std::vector<float>& samples, fasstv::SSTVDecode& sstvdec, std::filesystem::path& outputPath, int samplerate) {
	if (outputPath.empty())
		return;

	sstvdec.DecodeSamples(samples, samplerate);
	fasstv::SSTV::Mode* mode = sstvdec.GetMode();

	if (mode == nullptr) {
		fasstv::LogError("Decode failed, cannot export");
		return;
	}

	// for automatic file naming
	//if (!outputPath.has_extension()) {
		outputPath.replace_extension(".qoi");
	//}

	fasstv::LogInfo("Saving {}...", outputPath.c_str());
	std::ofstream file(outputPath.string(), std::ios::binary);
	if (outputPath.extension() == ".qoi")
		fasstv::PixelsToQOI(sstvdec.GetPixels(nullptr), mode->width, mode->lines, file);
	file.close();
}

int main(int argc, char** argv) {
	std::filesystem::path inputPath {};
	std::filesystem::path outputPath {};
	bool stretch = false;
	bool play = false;
	bool decode = false;
	bool separateScans = false;
	int resizeFlags = SWS_BICUBIC;
	float volume = 0.33f;
	int samplerate = 8000;

	fasstv::LoggerAttachStdout();
	fasstv::LogDebug("Built {} {}", __DATE__, __TIME__);

	fasstv::LogDebug("SDL {}, rev {}", SDL_VERSION, SDL_GetRevision());

	if (!SDL_Init(SDL_INIT_AUDIO /*| SDL_INIT_CAMERA*/)) {
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
			case 'd': {
				decode = true;
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
			case 'r': {
				samplerate = atoi(cag_option_get_value(&context));
				// clamp
				samplerate = samplerate < 4000 ? 4000 : samplerate;
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
	SDL_CameraID camId = devices[0];

	int formatCount = 0;
	SDL_CameraSpec desiredSpec = {};
	SDL_CameraSpec** camFormats = SDL_GetCameraSupportedFormats(camId, &formatCount);
	for (int i = 0; i < formatCount; i++) {
		SDL_CameraSpec* pSpec = camFormats[i];
		SDL_Log("Cam %d Mode %d: %dx%d @ %d/%d - %s", camId, i, pSpec->width, pSpec->height, pSpec->framerate_numerator, pSpec->framerate_denominator, SDL_GetPixelFormatName(pSpec->format));
		if (pSpec->width == 800 && pSpec->framerate_numerator == 30) {
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
	while (surfOrig == nullptr)
		surfOrig = SDL_AcquireCameraFrame(cam, nullptr);*/

	fasstv::Rect letterbox = fasstv::Rect::CreateLetterbox(mode->width, mode->lines, {0, 0, surfOrig->w, surfOrig->h});

	SDL_Surface* surfOut;

	if (!stretch)
		surfOut = fasstv::RescaleImage(surfOrig, letterbox.w, letterbox.h, resizeFlags);
	else
		surfOut = fasstv::RescaleImage(surfOrig, mode->width, mode->lines, resizeFlags);

	SDL_free(surfOrig);

	// do the signal
	sstvenc.SetSampleRate(samplerate);
	sstvenc.SetLetterbox(fasstv::Rect::CreateLetterbox(mode->width, mode->lines, {0, 0, surfOut->w, surfOut->h}));
	sstvenc.SetLetterboxLines(false);
	sstvenc.SetPixelProvider(&fasstv::GetSampleFromSurface);

	if (!outputPath.empty()) {
		if (separateScans) {
			int neededFiles = mode->instructions_looping.size() + 1;
			std::filesystem::path outPath = outputPath;

			for (int i = 0; i < neededFiles; i++) {
				sstvenc.SetInstructionTypeFilter(fasstv::SSTV::InstructionType::Any, i);
				outPath.replace_filename(outputPath.stem().string() + "-stem" + std::to_string(i) + outputPath.extension().string());
				OutputSamples(sstvenc, surfOut, outPath, samplerate, volume);
			}

			sstvenc.SetInstructionTypeFilter(fasstv::SSTV::InstructionType::InvalidInstructionType);
		}
		else {
			OutputSamples(sstvenc, surfOut, outputPath, samplerate, volume);
		}

		// decoding tests
		if (decode) {
			std::vector<float> samples;
			sstvenc.RunAllInstructions(samples, {0, 0, surfOut->w, surfOut->h});
			for (float& smp : samples)
				smp *= volume;

			fasstv::SSTVDecode& sstvdec = fasstv::SSTVDecode::The();
			OutputImage(samples, sstvdec, outputPath, samplerate);
		}
	}

	const size_t buff_size = 320;
	static float buff[buff_size];
	sstvenc.ResetInstructionProcessing();

	SDL_Event event;
	bool run = true;

	//SDL_Surface *surfFrame = nullptr/*, *surfOut = nullptr*/;
	//Uint64 timestampNS = 0;

	while (run) {
		while (SDL_PollEvent(&event) > 0) {
			switch (event.type) {
				case SDL_EVENT_QUIT:
					run = false;
					break;
			}

#ifdef FASSTV_DEBUG
			run = fasstv::SSTVDecode::The().debug_DebugWindowPump(&event);
#endif

			/*if (SDL_GetTicksNS() - timestampNS > 1000000000ul) {
				SDL_Surface* surfTemp = SDL_AcquireCameraFrame(cam, &timestampNS);
				fasstv::LogDebug("Trying for a frame, temp: {}, frame: {}", reinterpret_cast<void*>(surfTemp), reinterpret_cast<void*>(surfFrame));
				if (surfTemp) {
					fasstv::LogDebug("Got a new frame?");
					surfFrame = surfTemp;
					surfTemp = nullptr;
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
					surfFrame = nullptr;
				}

				timestampNS = SDL_GetTicksNS();
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
		}

#ifdef FASSTV_DEBUG
		fasstv::SSTVDecode::The().debug_DebugWindowRender();
#endif
	}

	SDL_free(surfOut);

	SDL_Quit();
	return EXIT_SUCCESS;
}