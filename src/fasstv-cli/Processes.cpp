// Created by block on 2025-12-16.

#include <stdlib.h>

#include <libfasstv/libfasstv.hpp>

#include <fasstv-cli/Options.hpp>
#include <fasstv-cli/Processes.hpp>

#include <shared/ExportUtilities.hpp>
#include <shared/ImageUtilities.hpp>
#include <shared/Logger.hpp>
#include <shared/Rect.hpp>

namespace fasstv::cli {

	Processes& Processes::The() {
		static Processes the;
		return the;
	}

	void Processes::OutputSamples(std::filesystem::path& outputPath) {
		if (outputPath.empty())
			return;

		// one-shot
		std::vector<float> samples;
		SSTVEncode::The().RunAllInstructions(samples, {0, 0, surf_out->w, surf_out->h});
		for (float& smp : samples)
			smp *= Options::options.volume;

		// for automatic file naming
		if (!outputPath.has_extension()) {
			outputPath.replace_extension(".wav");
		}

		LogInfo("Saving {}...", outputPath.c_str());
		std::ofstream file(outputPath.string(), std::ios::binary);

		//if (outputPath.extension() == ".mp3")
			//SamplesToAVCodec(samples, Options::options.encode.samplerate, outputPath.c_str(), file);
		//else
			SamplesToWAV(samples, Options::options.encode.samplerate, file);

		file.close();
		samples.clear();
	}

	void Processes::OutputImage(std::vector<float>* samples, std::filesystem::path& outputPath) {
		if (outputPath.empty())
			return;

		if (samples) {
			SSTVDecode::The().ResetDecoding();
			SSTVDecode::The().DecodeAllSamples(*samples);
		}
		// else, assume the decoding is already done or has errored out

		SSTV::Mode* mode = SSTVDecode::The().GetMode();

		if (mode == nullptr) {
			LogError("Decode failed, cannot export");
			return;
		}

		// for automatic file naming
		//if (!outputPath.has_extension()) {
		outputPath.replace_extension(".qoi");
		//}

		LogInfo("Saving {}...", outputPath.c_str());
		std::ofstream file(outputPath.string(), std::ios::binary);

		if (outputPath.extension() == ".qoi")
			PixelsToQOI(SSTVDecode::The().GetPixels(nullptr), mode->width, mode->lines, file);

		file.close();
	}

	SSTVEncode& Processes::Encode_Setup() {
		SSTVEncode& sstvenc = SSTVEncode::The();
		sstvenc.SetSampleRate(Options::options.encode.samplerate);
		sstvenc.SetPixelProvider(&GetSampleFromSurface);
		sstvenc.SetNoiseStrength(Options::options.encode.noise_strength);
		return sstvenc;
	}

	SSTVDecode& Processes::Decode_Setup(SSTV::Mode* expectedMode /*= nullptr*/) {
		SSTVDecode& sstvdec = SSTVDecode::The();
		sstvdec.SetSampleRate(Options::options.encode.samplerate);
		//sstvdec.SetExpectedMode(expectedMode, true);
		return sstvdec;
	}

	bool PrintSDLAudioDevices(SDL_AudioDeviceID* list, int count) {
		if (!list || count < 1) {
			LogError("Couldn't list devices, none returned!");
			return false;
		}

		LogInfo("Available microphones: ");
		for (int i = 0; i < count; i++) {
			SDL_AudioDeviceID id = list[i];
			LogInfo("  [{}] {}", i+1, SDL_GetAudioDeviceName(id));
		}

		return true;
	}

	int Processes::Audio_Setup() {
		SDL_AudioSpec spec {
			.format = SDL_AUDIO_F32,
			.channels = 1,
			.freq = Options::options.encode.samplerate
		};

		if (!SDL_Init(SDL_INIT_AUDIO)) {
			LogError("Couldn't initialize SDL: {}", SDL_GetError());
			return SDL_APP_FAILURE;
		}

		if (Options::options.play) {
			audio_stream_out = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
			if (!audio_stream_out) {
				LogError("Couldn't create speaker audio stream: {}", SDL_GetError());
				return SDL_APP_FAILURE;
			}

			LogInfo("Trying to play through {}...", SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(audio_stream_out)));

			bool resume = SDL_ResumeAudioStreamDevice(audio_stream_out);
			if (!resume) {
				LogError("Couldn't resume speaker audio stream: {}", SDL_GetError());
				return SDL_APP_FAILURE;
			}
		}

		int microphone_idx = Options::options.decode.microphone_idx;
		// we intentionally add 1 to this so that 0 can be the default

		SDL_AudioDeviceID deviceID = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
		int deviceCount = 0;
		SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&deviceCount);

		if (microphone_idx < 0)
			PrintSDLAudioDevices(devices, deviceCount);
		else if (microphone_idx > 0 && microphone_idx <= deviceCount)
			deviceID = devices[microphone_idx-1];

		SDL_free(devices);

		if (microphone_idx >= 0) {
			audio_stream_in = SDL_OpenAudioDeviceStream(deviceID, &spec, nullptr, nullptr);
			if (!audio_stream_in) {
				LogError("Couldn't create microphone audio stream: {}", SDL_GetError());
				return SDL_APP_FAILURE;
			}

			LogInfo("Trying to record {}...", SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(audio_stream_in)));

			bool resume = SDL_ResumeAudioStreamDevice(audio_stream_in);
			if (!resume) {
				LogError("Couldn't resume microphone audio stream: {}", SDL_GetError());
				return SDL_APP_FAILURE;
			}
		}

		return 0;
	}

	void Processes::Audio_PumpOutputStream() {
		SSTVEncode& sstvenc = SSTVEncode::The();

		if(audio_stream_out != nullptr) {
			const int minimum_audio = Options::options.encode.samplerate;
			if(SDL_GetAudioStreamAvailable(audio_stream_out) < minimum_audio) {
				if(!sstvenc.IsDone() && surf_out != nullptr) {
					sstvenc.PumpInstructionProcessing(&speaker_buffer[0], buffer_size, { 0, 0, surf_out->w, surf_out->h });
					for(float& smp : speaker_buffer)
						smp *= Options::options.volume;
					SDL_PutAudioStreamData(audio_stream_out, &speaker_buffer[0], sizeof(speaker_buffer));
				}
			}
		}
	}

	int Processes::Encode_SetModeRescaleAndLetterboxImage() {
		SSTVEncode& sstvenc = SSTVEncode::The();
		SSTV::Mode* mode = Options::options.mode;

		// load and scale image
		surf_orig = LoadImage(Options::options.inputPath);
		if (surf_orig == nullptr)
			return EXIT_FAILURE;

		if (Options::options.fasstv_mode == FASSTVMode::Transcode && Options::options.transcode.resize_mode_to_image) {
			origWidth = mode->width;
			origLines = mode->lines;

			mode->width = surf_orig->w;
			mode->lines = surf_orig->h;
		}

		// build instructions
		sstvenc.SetMode(mode);

		Rect letterbox = Rect::CreateLetterbox(mode->width, mode->lines, { 0, 0, surf_orig->w, surf_orig->h });

		if(!Options::options.encode.image_stretch)
			surf_out = RescaleImage(surf_orig, letterbox.w, letterbox.h, Options::options.encode.image_resize_method);
		else
			surf_out = RescaleImage(surf_orig, mode->width, mode->lines, Options::options.encode.image_resize_method);

		SDL_free(surf_orig);

		// set up letterboxing
		sstvenc.SetLetterbox(Rect::CreateLetterbox(mode->width, mode->lines, { 0, 0, surf_out->w, surf_out->h }));
		sstvenc.SetLetterboxLines(true);

		return EXIT_SUCCESS;
	}

	int Processes::ProcessEncode() {
		int res;

		res = Audio_Setup();
		if (res != EXIT_SUCCESS)
			return res;

		res = Encode_SetModeRescaleAndLetterboxImage();
		if (res != EXIT_SUCCESS)
			return res;

		SSTVEncode& sstvenc = Encode_Setup();
		SSTV::Mode* mode = sstvenc.GetMode();

		if (!Options::options.outputPath.empty()) {
			if (Options::options.encode.separate_scans) {
				int neededFiles = mode->instructions_looping.size() + 1;
				std::filesystem::path outPath = Options::options.outputPath;

				for (int i = 0; i < neededFiles; i++) {
					sstvenc.SetInstructionTypeFilter(SSTV::InstructionType::Any, i);
					outPath.replace_filename(Options::options.outputPath.stem().string() + "-stem" + std::to_string(i) + Options::options.outputPath.extension().string());
					OutputSamples(outPath);
				}

				sstvenc.SetInstructionTypeFilter(SSTV::InstructionType::InvalidInstructionType);
			}
			else {
				OutputSamples(Options::options.outputPath);
			}
		}

		// if we're not going to play (or eventually get camera frames) we can exit
		if (!Options::options.play)
			return EXIT_SUCCESS;

		// reset so we can play from the beginning
		sstvenc.ResetInstructionProcessing();

		//SDL_Surface *surfFrame = nullptr/*, *surfOut = nullptr*/;
		//Uint64 timestampNS = 0;

		while (sdl_run) {
			while (SDL_PollEvent(&event)) {
				switch (event.type) {
					case SDL_EVENT_QUIT:
						sdl_run = false;
						break;
				}
			}

			/*if (SDL_GetTicksNS() - timestampNS > 1000000000ul) {
				SDL_Surface* surfTemp = SDL_AcquireCameraFrame(cam, &timestampNS);
				LogDebug("Trying for a frame, temp: {}, frame: {}", reinterpret_cast<void*>(surfTemp), reinterpret_cast<void*>(surfFrame));
				if (surfTemp) {
					LogDebug("Got a new frame?");
					surfFrame = surfTemp;
					surfTemp = nullptr;
				}

				if (surfFrame) {
					Rect letterbox = Rect::CreateLetterbox(mode->width, mode->lines, {0, 0, surfFrame->w, surfFrame->h});

					if (surfOut) {
						SDL_free(surfOut);
						surfOut = nullptr;
					}

					if (!stretch)
						surfOut = RescaleImage(surfFrame, letterbox.w, letterbox.h, resizeFlags);
					else
						surfOut = RescaleImage(surfFrame, mode->width, mode->lines, resizeFlags);

					SDL_free(surfFrame);
					surfFrame = nullptr;
				}

				timestampNS = SDL_GetTicksNS();
			}*/

			Audio_PumpOutputStream();

			if (!sstvenc.HasStarted() || sstvenc.IsDone())
				sdl_run = false;
		}

		SDL_free(surf_out);
		SDL_Quit();

		// reset back to default
		if (origWidth == -1 || origLines == -1) {
			mode->width = origWidth;
			mode->lines = origLines;
		}

		return EXIT_SUCCESS;
	}

	int Processes::ProcessDecode() {
		int res = Audio_Setup();

		if (res != EXIT_SUCCESS)
			return res;

		bool runFromMic = true;

		SDL_AudioSpec audioSpec;
		float* audioBuf = nullptr;
		uint32_t audioBufLen = 0;
		uint32_t audioBufProgress = 0;
		std::vector<float> samples;

		bool loaded = SDL_LoadWAV(Options::options.inputPath.c_str(), &audioSpec, reinterpret_cast<uint8_t**>(&audioBuf), &audioBufLen);

		if (loaded) {
			runFromMic = false;

			LogInfo("Loaded {}", Options::options.inputPath.filename().string());
			LogDebug("Samplerate of {}, format {}", audioSpec.freq, SDL_GetAudioFormatName(audioSpec.format));

			// we need to change the global samplerate here
			Options::options.encode.samplerate = audioSpec.freq;

			audioBufLen = audioBufLen / sizeof(float);
			samples.assign(&audioBuf[0], &audioBuf[0] + audioBufLen);

			//std::ofstream file(Options::options.outputPath.replace_filename("garbage.wav"), std::ios::binary);
			//SamplesToWAV(samples, audioSpec.freq, file);
		}

		SSTVDecode& sstvdec = Decode_Setup(Options::options.mode);
		// reset so we can play from the beginning
		sstvdec.ResetDecoding();

		bool doTick = false;
		bool latchDoTick = true;

		while (sdl_run) {
			while (SDL_PollEvent(&event)) {
				switch (event.type) {
					case SDL_EVENT_QUIT:
						sdl_run = false;
						break;
					case SDL_EVENT_KEY_DOWN: {
						if (event.key.scancode == SDL_SCANCODE_P) {
							sstvdec.ResetDecoding();
							audioBufProgress = 0;
						}
						if (event.key.scancode == SDL_SCANCODE_N) {
							doTick = true;
							if (event.key.mod & SDL_KMOD_SHIFT)
								latchDoTick = !latchDoTick;
						}
						break;
					}
				}

#ifdef FASSTV_DEBUG
				if (sstvdec.debug_DebugWindowIsOpen())
					SSTVDecode::The().debug_DebugWindowPump(&event);
#endif
			}

			if (doTick) {
				if(runFromMic && audio_stream_in != nullptr) {
					const int minimum_audio = Options::options.encode.samplerate;
					while (SDL_GetAudioStreamAvailable(audio_stream_in) >= minimum_audio) {
						SDL_GetAudioStreamData(audio_stream_in, &mic_buffer[0], sizeof(mic_buffer));

						sstvdec.PumpDecoding(&mic_buffer[0], buffer_size);
					}
				}
				else if (!samples.empty()) {
					const int increment = buffer_size;

					if (audioBufProgress + increment < audioBufLen)
						sstvdec.PumpDecoding(&audioBuf[audioBufProgress], increment);
					audioBufProgress += increment;
				}

				if (latchDoTick)
					doTick = false;
			}

			bool considerClosing = sstvdec.IsDone() && sstvdec.HasDecodedImage();
#ifdef FASSTV_DEBUG
			considerClosing = considerClosing && !sstvdec.debug_DebugWindowIsOpen();
#endif

			if (considerClosing)
				sdl_run = false;

#ifdef FASSTV_DEBUG
			SSTVDecode::The().debug_DebugWindowRender();
#endif
		}

		//if (sstvdec.HasDecodedImage())
			//OutputImage(nullptr, Options::options.outputPath);

		if (surf_out)
			SDL_free(surf_out);
		if (audioBuf)
			SDL_free(audioBuf);
		SDL_Quit();

		return EXIT_SUCCESS;
	}

	int Processes::ProcessTranscode() {
		int res;

#ifdef FASSTV_DEBUG
		if (!SDL_Init(SDL_INIT_VIDEO)) {
			LogError("Couldn't initialize SDL: {}", SDL_GetError());
			return SDL_APP_FAILURE;
		}
#endif

		res = Audio_Setup();
		if (res != EXIT_SUCCESS)
			return res;

		res = Encode_SetModeRescaleAndLetterboxImage();
		if (res != EXIT_SUCCESS)
			return res;

		SSTVEncode& sstvenc = Encode_Setup();
		SSTV::Mode* mode = sstvenc.GetMode();

		SSTVDecode& sstvdec = Decode_Setup(mode);

		if (Options::options.outputPath.empty()) {
			// this mode is all about outputting a file... let's make a default
			Options::options.outputPath = Options::options.inputPath;

			// original filename + ".qoi"
			Options::options.outputPath.replace_filename(Options::options.inputPath.filename().string() + ".qoi");
		}

		std::vector<float> samples;
		SSTVEncode::The().RunAllInstructions(samples, {0, 0, surf_out->w, surf_out->h});
		for (float& smp : samples)
			smp *= Options::options.volume;

		OutputImage(&samples, Options::options.outputPath);
		//OutputSamples(Options::options.outputPath);

		if (Options::options.play)
			// reset so we can play from the beginning
			sstvenc.ResetInstructionProcessing();

		while (sdl_run) {
			while (SDL_PollEvent(&event)) {
				switch (event.type) {
					case SDL_EVENT_QUIT:
						sdl_run = false;
						break;
					case SDL_EVENT_KEY_DOWN: {
						if (event.key.scancode == SDL_SCANCODE_P) {
							if (!sstvenc.IsDone())
								sstvenc.FinishInstructionProcessing();
							else
								sstvenc.ResetInstructionProcessing();
						}
						break;
					}
				}

#ifdef FASSTV_DEBUG
				if (sstvdec.debug_DebugWindowIsOpen())
					SSTVDecode::The().debug_DebugWindowPump(&event);
#endif
			}

			Audio_PumpOutputStream();

			bool considerClosing = (!sstvenc.HasStarted() || sstvenc.IsDone()) && (!sstvdec.HasStarted() || sstvdec.IsDone());
#ifdef FASSTV_DEBUG
			considerClosing = considerClosing && !sstvdec.debug_DebugWindowIsOpen();
#endif

			if (considerClosing)
				sdl_run = false;

#ifdef FASSTV_DEBUG
			SSTVDecode::The().debug_DebugWindowRender();
#endif
		}

		SDL_free(surf_out);
		SDL_Quit();

		// reset back to default
		if (origWidth == -1 || origLines == -1) {
			mode->width = origWidth;
			mode->lines = origLines;
		}

		return EXIT_SUCCESS;
	}

}