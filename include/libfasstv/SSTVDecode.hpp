// Created by block on 2025-12-08.

#pragma once

#ifdef FASSTV_DEBUG
#include <SDL3/SDL.h>
#endif

#include <vector>

namespace fasstv {

	class SSTVDecode {
	public:
		static SSTVDecode& The();

		void DecodeSamples(std::vector<float>& samples, int samplerate, SSTV::Mode* expectedMode = nullptr);

	private:
		float AverageFreqAtArea(float pos_ms, int width_samples = 10);
		bool AverageFreqAtAreaExpected(float pos_ms, float freq_expected, float freq_margin = 50.f, int width_samples = 10, float* freq_back = nullptr);

#ifdef FASSTV_DEBUG
		float debug_AverageFreqAtArea(std::string_view text, float pos_ms, int width_samples = 10);
		bool debug_AverageFreqAtAreaExpected(std::string_view text, float pos_ms, float freq_expected, float freq_margin = 50.f, int width_samples = 10, float* freq_back = nullptr);

		SDL_Renderer* debug_DebugWindowSetup();

	public:
		bool debug_DebugWindowPump(SDL_Event* ev); // return false if done
		void debug_DebugWindowRender();

	private:
		void debug_DrawFrequencyReferenceLines();
		void debug_DrawFrequencyGraph();
		void debug_DrawBuffersToScreen();

		SDL_Renderer* debug_renderer = nullptr;
		int debug_windowDimensions[2] = { 2048, 768 };

		float debug_graphFreqYScale = 3.f;
		float debug_graphFreqXScale = 7.f;
		float debug_graphFreqYPos = 1000.f / debug_graphFreqYScale; // in hertz
		float debug_graphFreqXPos = 0.f; // in seconds
#endif

		float* work_buf = nullptr;
		size_t work_buf_size = 0;
		std::uint8_t* pixel_buf = nullptr;
		size_t pixel_buf_size = 0;

		int samplerate;
		std::vector<float> samples;
		std::vector<float> samples_freq;

		SSTV::Mode* ourMode = nullptr;

		bool hasDecoded	= false;
	};

} // namespace fasstv