// Created by block on 2025-12-08.

#pragma once

#ifdef FASSTV_DEBUG
#include <SDL3/SDL.h>
#endif

#include <vector>

namespace fasstv {

	#ifdef FASSTV_DEBUG
	struct AverageFreqDebugInfo {
		float pos_ms {};
		int width_samples {};
		float freq_expected {};
		float freq_margin {};
		float freq_back {}; // callback
		float ret {};
		std::string debug_text {};
	};

	// vaguely new (c++17) thing I didn't know about, thanks CLion
	// inline keyword works around the one definition rule. easier than defining and using an extern!
	inline std::vector<AverageFreqDebugInfo> debug_AverageFreqInfo{};
	#endif

	class SSTVDecode {
	public:
		static constexpr int NUM_WORK_BUFFERS = 3;
		static constexpr int NUM_CHANNELS = 3; // always 3 for RGB. just nice to reduce magic numbers

		static SSTVDecode& The();

		~SSTVDecode();

		void DecodeSamples(std::vector<float>& samples, int samplerate, SSTV::Mode* expectedMode = nullptr);

		SSTV::Mode* GetMode() const { return ourMode; }
		std::uint8_t* GetPixels(size_t* out_size) const;

	private:
		void FreeBuffers();

		float AverageFreqAtArea(float pos_ms, int width_samples = 10, std::string debug_text = "");
		bool AverageFreqAtAreaExpected(float pos_ms, float freq_expected, float freq_margin = 50.f, int width_samples = 10, float* freq_back = nullptr, std::string debug_text = "");

		float SamplesLengthInSeconds() const { return samples.size() / (float)samplerate; }

#ifdef FASSTV_DEBUG
		SDL_Renderer* debug_DebugWindowSetup();

	public:
		bool debug_DebugWindowPump(SDL_Event* ev); // return false if done
		void debug_DebugWindowRender();

	private:
		float debug_GetTimeAtMouse() const;
		int debug_GetSampleAtMouse(bool clamp = true) const;
		float debug_GetFreqAtMouse() const;

		float debug_GetScreenPosAtTime(float time) const;
		float debug_GetScreenPosAtFreq(float freq) const;
		SDL_FPoint debug_GetScreenPosAtTimeAndFreq(float time, float freq) const;

		void debug_DrawCursorInfo() const;
		void debug_DrawTimeReferenceLines() const;
		void debug_DrawFrequencyReferenceLines() const;
		void debug_ResetFrequencyGraphScale(bool fullScreen = false);
		void debug_DrawFrequencyGraph() const;
		void debug_DrawAverageFreqDisplay() const;
		void debug_DrawBuffersToScreen() const;

		inline float debug_GetTimeAtSample(const int smp) const { return smp / (float)samplerate; }
		inline int debug_GetSampleAtTime(const float time) const { return time * samplerate; }

		inline int debug_GetGraphXPosInSamples() const { return debug_graphFreqXPos * samplerate; }
		inline int debug_GetGraphWidthInSamples() const { return debug_windowDimensions[0] * debug_graphFreqXScale; }
		inline float debug_GetGraphWidthInSeconds() const { return debug_GetGraphWidthInSamples() / (float)samplerate; }
		inline float debug_GetGraphHeightInHertz() const { return debug_windowDimensions[1] * debug_graphFreqYScale; }

		SDL_Renderer* debug_renderer = nullptr;
		int debug_windowDimensions[2] = { 2048, 768 };

		float debug_graphFreqYScale = 2.f;
		float debug_graphFreqXScale = 4.f;
		float debug_graphFreqYPos = 1000.f; // in hertz
		float debug_graphFreqXPos = 0.f; // in seconds

		bool debug_drawBuffers = true;
		int debug_drawBuffersType = 0; // 0 - none, 1 - final, 2 - final + rgb, 3 - final + work, 4 - final + rgb + work
		int debug_drawAverageFreqType = 3; // 0 - none, 1 - avg, 2 - avg expected, 3 - both, 4 - with text
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