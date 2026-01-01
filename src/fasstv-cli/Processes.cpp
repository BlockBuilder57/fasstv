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

		if (outputPath.extension() == ".mp3")
			SamplesToAVCodec(samples, Options::options.encode.samplerate, file);
		else
			SamplesToWAV(samples, Options::options.encode.samplerate, file);

		file.close();
		samples.clear();
	}

	void Processes::OutputImage(std::vector<float>& samples, std::filesystem::path& outputPath) {
		if (outputPath.empty())
			return;

		SSTVDecode::The().DecodeSamples(samples, Options::options.encode.samplerate, Options::options.mode, true);
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

	int Processes::Audio_Setup() {
		SDL_AudioSpec spec {
			.format = SDL_AUDIO_F32,
			.channels = 1,
			.freq = Options::options.encode.samplerate
		};

		audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
		if (!audio_stream) {
			LogError("Couldn't create audio stream: {}", SDL_GetError());
			return SDL_APP_FAILURE;
		}

		LogInfo("Trying to play through {}...", SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(audio_stream)));

		bool resume = SDL_ResumeAudioStreamDevice(audio_stream);
		if (!resume) {
			LogError("Couldn't resume audio stream: {}", SDL_GetError());
			return SDL_APP_FAILURE;
		}

		return 0;
	}

	void Processes::Audio_PumpOutputStream() {
		SSTVEncode& sstvenc = SSTVEncode::The();

		if(audio_stream != nullptr) {
			const int minimum_audio = Options::options.encode.samplerate;
			if(SDL_GetAudioStreamAvailable(audio_stream) < minimum_audio) {
				if(!sstvenc.IsDone() && surf_out != nullptr) {
					sstvenc.PumpInstructionProcessing(&speaker_buffer[0], buffer_size, { 0, 0, surf_out->w, surf_out->h });
					for(float& smp : speaker_buffer)
						smp *= Options::options.volume;
					SDL_PutAudioStreamData(audio_stream, &speaker_buffer[0], sizeof(speaker_buffer));
				}
			}
		}
	}

	int Processes::Encode_RescaleAndLetterboxImage() {
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

		// set up the encoder
		sstvenc.SetSampleRate(Options::options.encode.samplerate);
		sstvenc.SetLetterbox(Rect::CreateLetterbox(mode->width, mode->lines, { 0, 0, surf_out->w, surf_out->h }));
		sstvenc.SetLetterboxLines(false);
		sstvenc.SetPixelProvider(&GetSampleFromSurface);
		sstvenc.SetNoiseStrength(Options::options.encode.noise_strength);

		return EXIT_SUCCESS;
	}

	int Processes::ProcessEncode() {
		if (Options::options.play) {
			if (!SDL_Init(SDL_INIT_AUDIO)) {
				LogError("Couldn't initialize SDL: {}", SDL_GetError());
				return SDL_APP_FAILURE;
			}

			int res = Audio_Setup();
			if (res != EXIT_SUCCESS)
				return res;
		}

		int res = Encode_RescaleAndLetterboxImage();
		if (res != EXIT_SUCCESS)
			return res;

		SSTVEncode& sstvenc = SSTVEncode::The();
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
		LogInfo("Decoding from files not yet implemented");

		return EXIT_SUCCESS;
	}

	int Processes::ProcessTranscode() {
#ifdef FASSTV_DEBUG
		if (!SDL_Init(SDL_INIT_VIDEO)) {
			LogError("Couldn't initialize SDL: {}", SDL_GetError());
			return SDL_APP_FAILURE;
		}
#endif

		if (Options::options.play) {
			if (!SDL_Init(SDL_INIT_AUDIO)) {
				LogError("Couldn't initialize SDL: {}", SDL_GetError());
				return SDL_APP_FAILURE;
			}

			int res = Audio_Setup();
			if (res != EXIT_SUCCESS)
				return res;
		}

		int res = Encode_RescaleAndLetterboxImage();
		if (res != EXIT_SUCCESS)
			return res;

		SSTVEncode& sstvenc = SSTVEncode::The();
		SSTVDecode& sstvdec = SSTVDecode::The();
		SSTV::Mode* mode = sstvenc.GetMode();

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

		OutputImage(samples, Options::options.outputPath);
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