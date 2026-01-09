// Created by block on 2025-12-08.

#pragma once

#ifdef FASSTV_DEBUG
#include <SDL3/SDL.h>
#endif

#include <magic_enum/magic_enum.hpp>

#include <vector>

#include "SSTVMetadata.hpp"

namespace fasstv {

	#ifdef FASSTV_DEBUG
	struct AverageFreqDebugInfo {
		int pos_samples {};
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
		static constexpr int NUM_CHANNELS = 4; // bumping to 4 to experiment with alpha values
		static constexpr int NUM_WORK_BUFFERS = NUM_CHANNELS;

		static SSTVDecode& The();

		~SSTVDecode();

		void SetSampleRate(int samplerate);
		void SetExpectedMode(SSTV::Mode* expectedMode, bool expectedFallback = false);

		bool GetModeFromDecoded();
		void BuildInstructionsAndBuffers();
		void MakeImageFromWorkBuffer(int startX = 0, int startY = 0);

		void ResetDecoding();
		void PumpDecoding(float* arr, size_t arr_len);
		void DecodeAllSamples(std::vector<float>& samples);

		SSTV::Mode* GetMode() const { return decoded_mode; }
		std::uint8_t* GetPixels(size_t* out_size) const;

		bool HasStarted() const { return has_started; }
		bool IsDone() const { return is_done; }
		bool HasDecodedImage() const { return has_decoded_image; }

	private:
		void FreeBuffers();

		float AverageFreqAtArea(int pos_samples, int width_samples = 10, std::string debug_text = "", bool debug_save = true);
		bool AverageFreqAtAreaExpected(int pos_samples, float freq_expected, float freq_margin = 50.f, float freq_margin_leniency = 1.f, int width_samples = 10, float* freq_back = nullptr, std::string debug_text = "", bool debug_save = true);

		inline float TotalSamplesLengthInSeconds() const { return samples.size() / (float)samplerate; }

		inline float SamplesToSeconds(const int smp) const { return smp / (float)samplerate; }
		inline int SecondsToSamples(const float time) const { return time * samplerate; }

		// identical to above
		inline float GetTimeAtSample(const int smp) const { return SamplesToSeconds(smp); }
		inline int GetSampleAtTime(const float time) const { return SecondsToSamples(time); }

		enum class DecodingState;
		void Decoding_SwitchState(DecodingState state);

#ifdef FASSTV_DEBUG
		SDL_Renderer* debug_DebugWindowSetup();

	public:
		void debug_DebugWindowPump(SDL_Event* ev); // return false if done
		void debug_DebugWindowRender() const;
		bool debug_DebugWindowIsOpen() const { return debug_window_open; }

	private:

		float debug_GetTimeAtMouse() const;
		int debug_GetSampleAtMouse(bool clamp = true) const;
		float debug_GetFreqAtMouse() const;

		float debug_GetScreenPosAtTime(float time) const;
		float debug_GetScreenPosAtSample(int smp) const;
		float debug_GetScreenPosAtFreq(float freq) const;
		SDL_FPoint debug_GetScreenPosAtTimeAndFreq(float time, float freq) const;

		void debug_DrawCursorInfo() const;
		void debug_DrawTimeReferenceLines() const;
		void debug_DrawFrequencyReferenceLines() const;
		void debug_ResetFrequencyGraphScale(bool fullScreen = false);
		void debug_DrawFrequencyGraph() const;
		void debug_DrawAverageFreqDisplay() const;
		void debug_DrawBuffersToScreen() const;
		void debug_DrawDecodingProgress() const;

		inline int debug_GetGraphXPosInSamples() const { return debug_graphFreqXPos * samplerate; }
		inline int debug_GetGraphWidthInSamples() const { return debug_windowDimensions[0] * debug_graphFreqXScale; }
		inline float debug_GetGraphWidthInSeconds() const { return debug_GetGraphWidthInSamples() / (float)samplerate; }
		inline float debug_GetGraphHeightInHertz() const { return debug_windowDimensions[1] * debug_graphFreqYScale; }

		SDL_Renderer* debug_renderer = nullptr;
		int debug_windowDimensions[2] = { 2048, 768 };
		bool debug_window_open = false;

		float debug_graphFreqYScale = 2.f;
		float debug_graphFreqXScale = 4.f;
		float debug_graphFreqYPos = 1000.f; // in hertz
		float debug_graphFreqXPos = 0.f; // in seconds

		int debug_drawBuffersType = 0; // 0 - none, 1 - final, 2 - final + rgb, 3 - final + work, 4 - final + rgb + work
		int debug_drawAverageFreqType = 0; // 0 - none, 1 - avg, 2 - avg expected, 3 - both, 4 - with text
		int debug_drawDecodingType = 0; // 0 - none, 1 - yes
#endif

		float* work_buf = nullptr;
		size_t work_buf_size = 0;
		std::uint8_t* pixel_buf = nullptr;
		size_t pixel_buf_size = 0;

		int samplerate;
		std::vector<float> samples;
		std::vector<float> samples_freq;

		SSTV::Mode* expected_mode = nullptr;
		bool expected_mode_fallback = false;

		enum class DecodingState {
			Invalid,

			PreStart,
			StartBuildInstructions,
			StartTryAcquire,
			StartTryAcquireByHeader,
			StartTryReadVIS,

			ScanSetup,
			ScanDoLines,

			FailureRecoverable,
			FailureCritical,

			Finish,

			DecodingStateFirst = PreStart,
			DecodingStateLast = Finish
		};

		int decoding_cur_sample = -1;
		DecodingState decoding_state {};
		int decoding_state_storage[4] {};
		std::vector<SSTV::Instruction> decoding_instructions {};
		SSTV::Instruction* decoding_instruction_last = nullptr;
		int decoding_pos[2] {};
		int decoding_instruction_idx = -1;
		int decoding_highest_field_encountered = -1;

		SSTV::Mode* decoded_mode = nullptr;
		SSTVMetadata::PerModeMetadata* decoded_mode_meta = nullptr;
		std::uint8_t decoded_vis_code = 0;
		bool decoded_vis_parity = false;

		bool has_started = false;
		bool is_done = false;
		bool has_decoded_image = false;
	};

} // namespace fasstv