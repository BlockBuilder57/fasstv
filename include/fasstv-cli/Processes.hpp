// Created by block on 2025-12-16.

#pragma once

#include <SDL3/SDL.h>

namespace fasstv::cli {

	class Processes {
	public:
		static Processes& The();

		int ProcessEncode();
		int ProcessDecode();
		int ProcessTranscode();

	private:
		void OutputSamples(std::filesystem::path& outputPath);
		void OutputImage(std::vector<float>& samples, std::filesystem::path& outputPath);

		int Audio_Setup();
		void Audio_PumpOutputStream();
		int Encode_RescaleAndLetterboxImage();

		bool sdl_run = true;
		SDL_Event event {};

		int origWidth = -1, origLines = -1;

		SDL_Surface* surf_orig = nullptr;
		SDL_Surface* surf_out = nullptr;

		SDL_AudioStream* audio_stream = nullptr;
		static constexpr size_t buffer_size = 320;
		float speaker_buffer[buffer_size] {};
	};

}