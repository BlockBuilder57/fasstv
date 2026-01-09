// Created by block on 2025-12-08.

#include <math.h>

#include <cstring>
#include <libfasstv/libfasstv.hpp>
#include <shared/Logger.hpp>

#include "../../third_party/PicoSSTV/cordic.h"
#include "../../third_party/PicoSSTV/half_band_filter2.h"
#include "fasstv-cli/Options.hpp"

#ifdef FASSTV_DEBUG
#include <SDL3/SDL.h>
#endif

// Majority of frequency tracking code by Jon Dawson
// https://github.com/dawsonjon/PicoSSTV

half_band_filter2 ssb_filter;
uint8_t ssb_phase = 0;
int16_t last_phase = 0;
int16_t* summed_audio_container = nullptr;
int32_t summed_audio_counter = 0;

int16_t rolling_freq_from_sample(int16_t audio, int samplerate)
{
	// shift frequency by +FS/4
	//       __|__
	//   ___/  |  \___
	//         |
	//   <-----+----->

	//        | ____
	//  ______|/    \
	//        |
	//  <-----+----->

	// filter -Fs/4 to +Fs/4

	//        | __
	//  ______|/  \__
	//        |
	//  <-----+----->

	ssb_phase = (ssb_phase + 1) & 3u;
	audio = audio >> 1;

	const int16_t audio_i[4] = {audio, 0, (int16_t)-audio, 0};
	const int16_t audio_q[4] = {0, (int16_t)-audio, 0, audio};
	int16_t ii = audio_i[ssb_phase];
	int16_t qq = audio_q[ssb_phase];
	ssb_filter.filter(ii, qq);

	// shift frequency by -FS/4
	//         | __
	//   ______|/  \__
	//         |
	//   <-----+----->

	//     __ |
	//  __/  \|______
	//        |
	//  <-----+----->

	const int16_t sample_i[4] = {(int16_t)-qq, (int16_t)-ii, qq, ii};
	const int16_t sample_q[4] = {ii, (int16_t)-qq, (int16_t)-ii, qq};
	int16_t i = sample_i[ssb_phase];
	int16_t q = sample_q[ssb_phase];

	uint16_t magnitude;
	int16_t phase;

	cordic_rectangular_to_polar(i, q, magnitude, phase);
	int16_t tracked_frequency = last_phase-phase;
	last_phase = phase;

	int16_t freq_at_sample = (int32_t)tracked_frequency*samplerate>>16;

	static uint32_t smoothed_freq_at_sample = 0;
	smoothed_freq_at_sample = ((smoothed_freq_at_sample << 3) + freq_at_sample - smoothed_freq_at_sample) >> 3;

	// incredibly lazy gate
	const int32_t gate_samples = (samplerate * 0.25f);

	if (!summed_audio_container) {
		auto size = gate_samples * sizeof(int16_t);
		summed_audio_container = static_cast<int16_t*>(malloc(size));
		memset(summed_audio_container, 0, size);
	}
	else {
		if (summed_audio_counter >= gate_samples)
			summed_audio_counter = 0;

		summed_audio_container[summed_audio_counter] = audio;

		int32_t sum = 0;
		for (int i = 0; i < gate_samples; i++)
			sum += abs(summed_audio_container[i]);

		summed_audio_counter++;

		if (sum < (INT16_MAX * 0.05f) * gate_samples)
			return 1000u;
	}

	// make sensible values for out of bounds
	if (smoothed_freq_at_sample < 1000u)
		return 1000u;
	else if (smoothed_freq_at_sample > 2400u)
		return 2400u;

	return std::min(std::max(smoothed_freq_at_sample, 1000u), 2400u);
}

namespace fasstv {

	SSTVDecode& SSTVDecode::The() {
		static SSTVDecode the;
		return the;
	}

	SSTVDecode::~SSTVDecode() {
		FreeBuffers();
	}

	void SSTVDecode::SetSampleRate(int samplerate) {
		this->samplerate = samplerate;
	}

	void SSTVDecode::SetExpectedMode(SSTV::Mode* expectedMode, bool expectedFallback /*= false*/) {
		expected_mode = expectedMode;
		expected_mode_fallback = expectedFallback;
	}

	float SSTVDecode::AverageFreqAtArea(int pos_samples, int width_samples /*= 10*/, std::string debug_text /*= ""*/, bool debug_save /*= true*/) {
		if (width_samples <= 1) {
			// just get the sample at pos_ms
			float smp = samples_freq[pos_samples];

#ifdef FASSTV_DEBUG
			if (debug_save)
				debug_AverageFreqInfo.emplace_back(pos_samples, width_samples, NAN, NAN, smp, smp, debug_text);
#endif

			return smp;
		}

		int rangeCenter = pos_samples;
		int rangeMin = rangeCenter - (width_samples / 2);
		int rangeMax = rangeCenter + (width_samples / 2);

		//LogDebug("AverageFreqAtArea checking {}-{}", rangeMin, rangeMax);

		std::vector<float> samplesToAverage;
		samplesToAverage.resize(rangeMax - rangeMin);

		float avg = 0;
		for (int i = 0; i < samplesToAverage.size(); i++) {
			int smpIdx = rangeMin + i;
			if (smpIdx >= 0 && smpIdx < samples_freq.size())
				samplesToAverage[i] = samples_freq[smpIdx];
			else
				samplesToAverage[i] = -1;

			avg += samplesToAverage[i];
		}
		avg /= samplesToAverage.size();

#ifdef FASSTV_DEBUG
		if (debug_save)
			debug_AverageFreqInfo.emplace_back(pos_samples, width_samples, NAN, NAN, avg, avg, debug_text);
#endif

		return avg;
	}

	bool SSTVDecode::AverageFreqAtAreaExpected(int pos_samples, float freq_expected, float freq_margin /*= 50.f*/, float freq_margin_leniency /*= 1.f*/, int width_samples /*= 10*/, float* freq_back /*= nullptr*/, std::string debug_text /*= ""*/, bool debug_save /*= true*/) {
		if (freq_expected < 0) {
			LogError("Looking for a negative frequency...?");
			return false;
		}

		float avg = AverageFreqAtArea(pos_samples, width_samples, "", debug_save);

		if (freq_back)
			*freq_back = avg;

		bool tooBig = avg > freq_expected + (freq_margin / 2);
		bool tooSmall = avg < freq_expected - (freq_margin / 2);
		bool tooManyErrantSamples = false;

		if (freq_margin_leniency < 1.f) {
			int rangeCenter = pos_samples;
			int rangeMin = rangeCenter - (width_samples / 2);
			int rangeMax = rangeCenter + (width_samples / 2);

			rangeMin = std::clamp(rangeMin, 0, (int)samples_freq.size() - 1);
			rangeMax = std::clamp(rangeMax, 0, (int)samples_freq.size() - 1);

			int errantSamples = 0;

			for (int i = rangeMin; i < rangeMax; i++) {
				if (samples_freq[i] > freq_expected + (freq_margin / 2) || samples_freq[i] < freq_expected - (freq_margin / 2))
					errantSamples++;
			}

			if ((float)errantSamples / (float)width_samples > freq_margin_leniency)
				tooManyErrantSamples = true;
		}

		bool result = !(tooBig || tooSmall || tooManyErrantSamples);

#ifdef FASSTV_DEBUG
		if (debug_save)
			debug_AverageFreqInfo.emplace_back(pos_samples, width_samples, freq_expected, freq_margin, avg, result, debug_text);
#endif

		return result;
	}

#ifdef FASSTV_DEBUG

	SDL_Renderer* SSTVDecode::debug_DebugWindowSetup() {
		if (!SDL_WasInit(0)) {
			LogDebug("SDL was not init, can't open decode debug window");
			return nullptr;
		}

		if (debug_renderer != nullptr) {
			return nullptr;
		}

		SDL_Window* window;
		SDL_CreateWindowAndRenderer("fasstv decoding", debug_windowDimensions[0], debug_windowDimensions[1], 0, &window, &debug_renderer);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
		SDL_GL_SetSwapInterval(1);

		SDL_SetRenderDrawColor(debug_renderer, 0, 0, 0, 255);
		SDL_RenderClear(debug_renderer);
		SDL_RenderPresent(debug_renderer);

		SDL_SetRenderDrawBlendMode(debug_renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 255);

		debug_ResetFrequencyGraphScale();

		debug_window_open = true;
		return debug_renderer;
	}

	void SSTVDecode::debug_DebugWindowPump(SDL_Event* ev) {
		if (!SDL_WasInit(0) || debug_renderer == nullptr) {
			debug_window_open = false;
			return;
		}

		if (ev->type == SDL_EVENT_QUIT) {
			debug_window_open = false;
			return;
		}

		const float windowWidthInSeconds = debug_GetGraphWidthInSeconds();
		float posShift = 0.1f * windowWidthInSeconds; // seconds!
		float scaleChangeUp = debug_graphFreqXScale;
		float scaleChangeDown = debug_graphFreqXScale * 0.5f;
		bool scaleChanged = false;

		float mouseX, mouseY;
		SDL_GetMouseState(&mouseX, &mouseY);
		static float mouseXScreenPctCenter = NAN;
		static float mouseDraggingCenter = NAN; // seconds
		float mouseXScreenPct = mouseX / debug_windowDimensions[0];
		bool scaleChangedByMouse = false;

		if (ev->type == SDL_EVENT_KEY_DOWN) {
			if (ev->key.mod & SDL_KMOD_SHIFT)
				posShift = 1.0f * windowWidthInSeconds;
			else if (ev->key.mod & SDL_KMOD_CTRL)
				posShift = 1.f / samplerate; // one sample
			else if (ev->key.mod & SDL_KMOD_ALT)
				if (decoded_mode_meta)
					posShift = (decoded_mode_meta->loop_length_ms / 1000.f); // one scan

			scaleChangeUp = ev->key.mod & SDL_KMOD_SHIFT ? scaleChangeUp : 0.1f;
			scaleChangeDown = ev->key.mod & SDL_KMOD_SHIFT ? scaleChangeDown : 0.1f;

			int typeChange = ev->key.mod & SDL_KMOD_SHIFT ? -1 : 1;

			switch (ev->key.scancode) {
				case SDL_SCANCODE_Z:
					debug_ResetFrequencyGraphScale();
					break;
				case SDL_SCANCODE_X:
					debug_ResetFrequencyGraphScale(true);
					break;
				case SDL_SCANCODE_V:
					if (decoded_mode_meta)
						debug_graphFreqXScale = ((decoded_mode_meta->loop_length_ms / 1000.f) * samplerate) / (float)debug_windowDimensions[0];
					break;
				case SDL_SCANCODE_UP:
				case SDL_SCANCODE_W:
					debug_graphFreqXScale += scaleChangeUp;
					if (scaleChangeUp != 0)
						scaleChanged = true;
					break;
				case SDL_SCANCODE_DOWN:
				case SDL_SCANCODE_S:
					debug_graphFreqXScale -= scaleChangeDown;
					if (scaleChangeDown != 0)
						scaleChanged = true;
					break;
				case SDL_SCANCODE_LEFT:
				case SDL_SCANCODE_A:
					debug_graphFreqXPos -= posShift;
					break;
				case SDL_SCANCODE_RIGHT:
				case SDL_SCANCODE_D:
					debug_graphFreqXPos += posShift;
					break;
				case SDL_SCANCODE_0:
				case SDL_SCANCODE_HOME:
					debug_graphFreqXPos = -windowWidthInSeconds / 2.f;
					break;
				case SDL_SCANCODE_9:
				case SDL_SCANCODE_END:
					debug_graphFreqXPos = TotalSamplesLengthInSeconds() - (windowWidthInSeconds / 2.f);
					break;
				case SDL_SCANCODE_1:
					debug_drawBuffersType = debug_drawBuffersType + typeChange > 4 || debug_drawBuffersType + typeChange < 0 ? 0 : debug_drawBuffersType + typeChange;
					break;
				case SDL_SCANCODE_2:
					debug_drawAverageFreqType = debug_drawAverageFreqType + typeChange > 4 || debug_drawAverageFreqType + typeChange < 0 ? 0 : debug_drawAverageFreqType + typeChange;
					break;
				case SDL_SCANCODE_3:
					debug_drawDecodingType = debug_drawDecodingType + typeChange > 1 || debug_drawDecodingType + typeChange < 0 ? 0 : debug_drawDecodingType + typeChange;
					break;
			}
		}
		else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
			if (fabs(ev->wheel.y) > 0.f) {
				float scrollScale = debug_graphFreqXScale <= 1.0f ? debug_graphFreqXScale : 1.0f;
				scrollScale *= fabs(ev->wheel.y);

				if (ev->wheel.y < 0)
					debug_graphFreqXScale += scaleChangeUp * scrollScale;
				else
					debug_graphFreqXScale -= scaleChangeDown * scrollScale;

				scaleChanged = true;
				scaleChangedByMouse = true;
			}
			if (fabs(ev->wheel.x) > 0.f) {
				debug_graphFreqXPos += ev->wheel.x * posShift;
			}
		}
		else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			switch (ev->button.button) {
				case SDL_BUTTON_MIDDLE:
					mouseXScreenPctCenter = mouseXScreenPct;
					mouseDraggingCenter = debug_graphFreqXPos;
					break;
			}
		}
		else if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP) {
			switch (ev->button.button) {
				case SDL_BUTTON_MIDDLE:
					mouseXScreenPctCenter = NAN;
					mouseDraggingCenter = NAN;
					break;
			}
		}

		if (!isnan(mouseDraggingCenter)) {
			float diff = mouseXScreenPctCenter - mouseXScreenPct;
			debug_graphFreqXPos = mouseDraggingCenter + (diff * windowWidthInSeconds);
		}

		if (scaleChanged) {
			// calculate a new window width, shift over by half to scale to center of screen
			float windowWidthInSecondsNew = debug_GetGraphWidthInSeconds();
			float diff = windowWidthInSeconds - windowWidthInSecondsNew;
			debug_graphFreqXPos += diff * ((!isnan(mouseDraggingCenter) || scaleChangedByMouse) ? mouseXScreenPct : 0.5f);
		}

		// clamp
		//debug_graphFreqXPos = std::clamp(debug_graphFreqXPos, -windowWidthInSeconds / 2.f, TotalSamplesLengthInSeconds() - (windowWidthInSeconds / 2.f));
		//debug_graphFreqYPos = std::clamp(debug_graphFreqYPos, 0.f, 3000.f);

		// max scale is window width * 2
		float maxXScale = (samples_freq.size() / (float)debug_windowDimensions[0]) * 2.0f;
		if (maxXScale <= 0) // ...unless we have no samples
			maxXScale = 1.0f;

		//debug_graphFreqXScale = std::clamp(debug_graphFreqXScale, 0.f, maxXScale);
	}

	float SSTVDecode::debug_GetTimeAtMouse() const {
		float x, y;
		SDL_GetMouseState(&x, &y);

		return (x * debug_graphFreqXScale / samplerate) + debug_graphFreqXPos;
	}

	int SSTVDecode::debug_GetSampleAtMouse(bool clamp /*= true*/) const {
		int val = GetSampleAtTime(debug_GetTimeAtMouse());

		if (!clamp && (val < 0 || val > samples_freq.size() - 1))
			return -1;

		if (samples_freq.empty())
			return -1;

		return std::clamp<int>(val, 0, samples_freq.size() - 1);
	}

	float SSTVDecode::debug_GetFreqAtMouse() const {
		float x, y;
		SDL_GetMouseState(&x, &y);

		return ((debug_windowDimensions[1] - y) * debug_graphFreqYScale) + debug_graphFreqYPos;
	}

	float SSTVDecode::debug_GetScreenPosAtTime(float time) const {
		return ((time - debug_graphFreqXPos) * samplerate) / debug_graphFreqXScale;
	}

	float SSTVDecode::debug_GetScreenPosAtSample(int smp) const {
		return (smp - (debug_graphFreqXPos * samplerate)) / debug_graphFreqXScale;
	}

	float SSTVDecode::debug_GetScreenPosAtFreq(float freq) const {
		return debug_windowDimensions[1] - (freq / debug_graphFreqYScale) + (debug_graphFreqYPos/debug_graphFreqYScale);
	}

	SDL_FPoint SSTVDecode::debug_GetScreenPosAtTimeAndFreq(float time, float freq) const {
		return {debug_GetScreenPosAtTime(time), debug_GetScreenPosAtFreq(freq)};
	}

	void SSTVDecode::debug_DebugWindowRender() const {
		if (!SDL_WasInit(0) || debug_renderer == nullptr)
			return;

		debug_DrawFrequencyGraph();

		// draw some corner stuff
		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 127);

		// draw pos and scale
		SDL_RenderDebugTextFormat(debug_renderer, 0, debug_windowDimensions[1] - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE, "Scale: %.2f (%d samples wide)", debug_graphFreqXScale, debug_GetGraphWidthInSamples());

		// draw decoding state
		if (decoding_state >= DecodingState::DecodingStateFirst && decoding_state <= DecodingState::DecodingStateLast) {
			SDL_RenderDebugText(debug_renderer, 0, debug_windowDimensions[1] - (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 4), "Decoding:");
			SDL_RenderDebugTextFormat(debug_renderer, 0, debug_windowDimensions[1] - (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 3), "    State: %s [%d, %d, %d, %d]", magic_enum::enum_name(decoding_state).data(), decoding_state_storage[0], decoding_state_storage[1], decoding_state_storage[2], decoding_state_storage[3]);
			SDL_RenderDebugTextFormat(debug_renderer, 0, debug_windowDimensions[1] - (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 2), "    Mode: %s", decoded_mode ? decoded_mode->name.c_str() : "unk");
		}

		debug_DrawCursorInfo();
		debug_DrawAverageFreqDisplay();
		debug_DrawDecodingProgress();
		debug_DrawBuffersToScreen();

		SDL_RenderPresent(debug_renderer);
		SDL_SetRenderDrawColor(debug_renderer, 0, 0, 0, 255);
		SDL_RenderClear(debug_renderer);

		return;
	}

	void SSTVDecode::debug_DrawCursorInfo() const {
		if (!SDL_WasInit(0) || debug_renderer == nullptr)
			return;

		float x, y;
		SDL_MouseButtonFlags mouseFlags = SDL_GetMouseState(&x, &y);
		bool leftClick = mouseFlags & SDL_BUTTON_MASK(SDL_BUTTON_LEFT);

		int smp = debug_GetSampleAtMouse(false);
		float freq = debug_GetFreqAtMouse();

		// adjust to top of mouse
		float textX = x + 2, textY = y - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE - 2;

		if(leftClick && smp != -1) {
			freq = samples_freq[smp];
			float freqOnScreen = debug_GetScreenPosAtFreq(freq);

			if(y < freqOnScreen)
				textY = freqOnScreen - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE - 2;
			else
				textY = freqOnScreen + (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 2) + 2;

			SDL_RenderLine(debug_renderer, x, y, x, freqOnScreen);
		}

		// clamp textY from going off screen
		textY = std::clamp<float>(textY, 0, debug_windowDimensions[1] - (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 1));

		if(smp != -1)
			SDL_RenderDebugTextFormat(debug_renderer, textX, textY - (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 2), "%dsmp", smp);
		SDL_RenderDebugTextFormat(debug_renderer, textX, textY - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE, "%.3fs", debug_GetTimeAtMouse());
		SDL_RenderDebugTextFormat(debug_renderer, textX, textY, "%.1fHz", freq);
	}

	void SSTVDecode::debug_DrawTimeReferenceLines() const {
		if (!SDL_WasInit(0))
			return;

		int startSec = std::floor(debug_graphFreqXPos);
		int endSec = startSec + std::ceil(debug_GetGraphWidthInSeconds()) + 1;
		float divisions = 1.f / powf(10, floor(log10(debug_GetGraphWidthInSeconds() * 0.5f)));
		if (divisions > 1000)
			divisions = 1000;

		const char* scales[4] = {"%.0fs", "%.1fs", "%.2fs", "%.3fs"};
		int scalesIdx = std::clamp<int>(log10(divisions), 0, (sizeof(scales) / sizeof(char*)) - 1);

		//LogDebug("{}, {}", divisions, log10(divisions));

		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 60);

		for (int t2 = startSec * divisions; t2 < endSec * divisions; t2++) {
			float timeX = debug_GetScreenPosAtTime(t2 / divisions);

			SDL_RenderDebugTextFormat(debug_renderer, timeX + 1, 1, scales[scalesIdx], t2 / divisions);

			for (int i = 0; i < debug_windowDimensions[1]; i += 2) {
				// cool alpha fade, but too extra: ((abs(i - (debug_windowDimensions[1] / 2)) / (float)debug_windowDimensions[1])) * 60
				SDL_RenderPoint(debug_renderer, timeX, i);
			}
		}

	}

	void SSTVDecode::debug_DrawFrequencyReferenceLines() const {
		if (!SDL_WasInit(0))
			return;

		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 60);
		const int reflines[7] = { 1100, 1200, 1300, 1500, 1900, 2100, 2300 };
		for (int num : reflines) {
			float freqOnScreen = debug_GetScreenPosAtFreq(num);

			SDL_RenderDebugTextFormat(debug_renderer, 0, freqOnScreen + 1, "%dHz", num);

			for (int i = 0; i < debug_windowDimensions[0]; i += 2)
				SDL_RenderPoint(debug_renderer, i, freqOnScreen);
		}
	}

	void SSTVDecode::debug_ResetFrequencyGraphScale(bool fullScreen /*= false*/) {
		// set scale: 1000-2400Hz, 1 second of samples
		debug_graphFreqXPos = 0.f;
		debug_graphFreqXScale = (fullScreen && samples.size() > 0 ? samples.size() : samplerate) / (float)debug_windowDimensions[0];
		debug_graphFreqYPos = 1000.f;
		debug_graphFreqYScale = 1400.f / debug_windowDimensions[1];
	}

	void SSTVDecode::debug_DrawFrequencyGraph() const {
		if (!SDL_WasInit(0))
			return;

		// debug samples and freq to screen
		float startSample = debug_GetGraphXPosInSamples();
		float endSample = std::clamp<float>(startSample + (debug_graphFreqXScale * debug_windowDimensions[0]), 0, samples.size());
		float stepSample = debug_graphFreqXScale;

		float lastSample = -1;
		float lastFreq = -1;
		float lastAmp = -1;

		bool sillyCircumstances = stepSample == 0 || (int)startSample == (int)endSample;

		SDL_SetRenderDrawColor(debug_renderer, 127, 127, 127, 255);

		for(float p = startSample; p < endSample || sillyCircumstances; p += stepSample) {
			// prevent infinite lock from floating point problems or 0 stepSample
			if (sillyCircumstances || p == p + stepSample) {
				// okay I want to get extra here

				const float length = std::clamp<float>(std::min(debug_windowDimensions[0], debug_windowDimensions[1]) - 128, 32.f, 128.f);
				const float sinFactor = 0.8660254f; // sin(60deg)
				const float cosFactor = 0.5f;       // cos(60deg)

				const float triangleOrigin[2] = {(debug_windowDimensions[0] / 2), (debug_windowDimensions[1] / 2)};

				const float a[2] = {(debug_windowDimensions[0] / 2), (debug_windowDimensions[1] / 2) - length};
				const float b[2] = {(debug_windowDimensions[0] / 2) + (sinFactor * length), (debug_windowDimensions[1] / 2) + (cosFactor * length)};
				const float c[2] = {(debug_windowDimensions[0] / 2) - (sinFactor * length), (debug_windowDimensions[1] / 2) + (cosFactor * length)};

				const float exclamationTop[2] = {(debug_windowDimensions[0] / 2), (debug_windowDimensions[1] / 2) - (length * 0.66f)};
				const float exclamationDot[2] = {(debug_windowDimensions[0] / 2), (debug_windowDimensions[1] / 2) + (cosFactor * length / 2)};
				const float exclamationDotSize = 5.f;

				SDL_RenderLine(debug_renderer, a[0], a[1], b[0], b[1]);
				SDL_RenderLine(debug_renderer, b[0], b[1], c[0], c[1]);
				SDL_RenderLine(debug_renderer, c[0], c[1], a[0], a[1]);

				SDL_RenderLine(debug_renderer, triangleOrigin[0], triangleOrigin[1], exclamationTop[0], exclamationTop[1]);
				SDL_RenderLine(debug_renderer, exclamationDot[0] - exclamationDotSize, exclamationDot[1] - exclamationDotSize, exclamationDot[0] + exclamationDotSize, exclamationDot[1] + exclamationDotSize);
				SDL_RenderLine(debug_renderer, exclamationDot[0] + exclamationDotSize, exclamationDot[1] - exclamationDotSize, exclamationDot[0] - exclamationDotSize, exclamationDot[1] + exclamationDotSize);

				SDL_RenderDebugText(debug_renderer, c[0], c[1] + 4, "Invalid graph!");

				const char* why = nullptr;
				if (stepSample == 0 || ((int)startSample == (int)endSample && (int)startSample != samples_freq.size()))
					why = "(Zero scale)";
				else if (startSample < 0 || ((int)startSample == (int)endSample && (int)startSample == samples_freq.size()))
					why = "(Out of bounds)";
				else if (p == p + stepSample)
					why = "(Float imprecision)";
				else
					why = "(Dunno)";

				SDL_RenderDebugText(debug_renderer, c[0], c[1] + 4 + SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 4, why);

				break;
			}

			// do nothing while we're off screen
			if (p < 0)
				continue;

			float frequency = samples_freq[p];
			float amplitude = samples[p];

			if (p == startSample) {
				lastSample = p;
				lastFreq = frequency;
				lastAmp = amplitude;
			}

			float lastX = (lastSample - startSample) / debug_graphFreqXScale;
			float newX = (p - startSample) / debug_graphFreqXScale;

			float lastFreqY = debug_windowDimensions[1] - (lastFreq / debug_graphFreqYScale) + (debug_graphFreqYPos/debug_graphFreqYScale);
			float newFreqY = debug_windowDimensions[1] - (frequency / debug_graphFreqYScale) + (debug_graphFreqYPos/debug_graphFreqYScale);
			float lastAmpY = (debug_windowDimensions[1] / 2) - (lastAmp * debug_windowDimensions[1] * 0.4f);
			float newAmpY = (debug_windowDimensions[1] / 2) - (amplitude * debug_windowDimensions[1] * 0.4f);

			SDL_SetRenderDrawColor(debug_renderer, 255, 0, 0, 15);
			SDL_RenderLine(debug_renderer, lastX, lastAmpY, newX, newAmpY);

			SDL_SetRenderDrawColor(debug_renderer, 127, 127, 127, 255);
			SDL_RenderLine(debug_renderer, lastX, lastFreqY, newX, newFreqY);

			lastSample = p;
			lastFreq = frequency;
			lastAmp = amplitude;
		}

		debug_DrawTimeReferenceLines();
		debug_DrawFrequencyReferenceLines();
	}

	void SSTVDecode::debug_DrawAverageFreqDisplay() const {
		if (!SDL_WasInit(0) || debug_drawAverageFreqType <= 0)
			return;

		const float viewingMargin = 0; //debug_GetGraphWidthInSeconds() / 16.f;

		for (int i = 0; i < debug_AverageFreqInfo.size(); i++) {
		 	auto& info = debug_AverageFreqInfo[i];

			int textWidth = 0;
			if (debug_drawAverageFreqType >= 4)
				textWidth = info.debug_text.size() * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

		 	// haven't reached our window yet...
		 	if ((info.pos_samples + (info.width_samples * 1)) / (float)samplerate <= debug_graphFreqXPos + viewingMargin - textWidth)
		 		continue;

		 	// we've gone too far! stop
		 	if ((info.pos_samples - (info.width_samples * 1)) / (float)samplerate >= debug_graphFreqXPos + debug_GetGraphWidthInSeconds() - viewingMargin + textWidth)
		 		break;

			if (isnan(info.freq_expected)) {
				// AverageFreqAtArea
				if (debug_drawAverageFreqType == 1 || debug_drawAverageFreqType >= 3) {
					float sampCenter = debug_GetScreenPosAtSample(info.pos_samples);
					float sampMin = sampCenter - (info.width_samples / debug_graphFreqXScale / 2);
					float sampMax = sampCenter + (info.width_samples / debug_graphFreqXScale / 2);

					int freqYObserved = debug_GetScreenPosAtFreq(info.freq_back);

					// draw width lines
					const int lineHeight = 5;
					SDL_SetRenderDrawColor(debug_renderer, 255, 0, 255, 255);
					SDL_RenderLine(debug_renderer, sampMin, freqYObserved - lineHeight, sampMin, freqYObserved + lineHeight);
					SDL_RenderLine(debug_renderer, sampMax, freqYObserved - lineHeight, sampMax, freqYObserved + lineHeight);
					SDL_RenderLine(debug_renderer, sampMax, freqYObserved - lineHeight, sampMax, freqYObserved + lineHeight);

					// draw observed
					SDL_SetRenderDrawColor(debug_renderer, 255, 255, 0, 255);
					SDL_RenderLine(debug_renderer, sampMax, freqYObserved, sampMax, freqYObserved);

					if (!info.debug_text.empty() && debug_drawAverageFreqType >= 4)
						SDL_RenderDebugText(debug_renderer, sampMin, freqYObserved, info.debug_text.c_str());
				}
			}
			else {
				// AverageFreqAtAreaExpected
				if (debug_drawAverageFreqType >= 2) {
					float sampCenter = debug_GetScreenPosAtSample(info.pos_samples);
					float sampMin = sampCenter - (info.width_samples / debug_graphFreqXScale / 2);
					float sampMax = sampCenter + (info.width_samples / debug_graphFreqXScale / 2);

					int freqYExpected = debug_GetScreenPosAtFreq(info.freq_expected);
					int freqYObserved = debug_GetScreenPosAtFreq(info.freq_back);
					int freqYMargin = (info.freq_margin / 2)/debug_graphFreqYScale;

					// draw expected line
					SDL_SetRenderDrawColor(debug_renderer, 255, 0, 255, 255);
					SDL_RenderLine(debug_renderer, sampMin, freqYExpected, sampMax, freqYExpected);
					// draw observed line
					SDL_SetRenderDrawColor(debug_renderer, 0, 0, 255, 255);
					SDL_RenderLine(debug_renderer, sampMin, freqYObserved, sampMax, freqYObserved);

					// draw rectangle
					if (info.ret)
						SDL_SetRenderDrawColor(debug_renderer, 40, 127, 10, 255);
					else
						SDL_SetRenderDrawColor(debug_renderer, 255, 0, 0, 255);

					SDL_FRect rect {sampMin, freqYExpected - freqYMargin, sampMax - sampMin, freqYMargin * 2};
					SDL_RenderRect(debug_renderer, &rect);

					if (!info.debug_text.empty() && debug_drawAverageFreqType >= 4)
						SDL_RenderDebugText(debug_renderer, sampMin, freqYExpected + freqYMargin + 1, info.debug_text.c_str());
				}
			}
		}
	}

	void SSTVDecode::debug_DrawBuffersToScreen() const {
		if (!SDL_WasInit(0) || debug_drawBuffersType <= 0)
			return;

		if (!pixel_buf || !work_buf)
			return;

		int xOff = 0;
		int yOff = 0;

		int modePixelCount = decoded_mode->width * decoded_mode->lines;

		const int padding = 8;

		// draw pixels
		for (int i = 0; i < modePixelCount * NUM_CHANNELS; i += NUM_CHANNELS) {
			int idxLocal = i / NUM_CHANNELS;

			int x = idxLocal % decoded_mode->width;
			int y = idxLocal / decoded_mode->width;

			std::uint8_t* pixel = &pixel_buf[i];

			SDL_SetRenderDrawColor(debug_renderer, pixel[0], pixel[1], pixel[2], pixel[3]);
			SDL_RenderPoint(debug_renderer, xOff + x, yOff + y);

			// draw individual channels
			if (debug_drawBuffersType == 2 || debug_drawBuffersType >= 4) {
				for (int j = 0; j < NUM_CHANNELS; j++) {
					xOff = (decoded_mode->width + padding) * (j+1);

					SDL_SetRenderDrawColor(debug_renderer, pixel[j], pixel[j], pixel[j], 255);
					SDL_RenderPoint(debug_renderer, xOff + x, yOff + y);
				}

				xOff = 0;
			}
		}

		const char* named_channels = "RGBA";
		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 255);
		SDL_RenderDebugText(debug_renderer, xOff, yOff + decoded_mode->lines, named_channels);
		if (debug_drawBuffersType == 2 || debug_drawBuffersType >= 4) {
			for (int j = 0; j < NUM_CHANNELS; j++) {
				xOff = (decoded_mode->width + padding) * (j+1);
				SDL_RenderDebugTextFormat(debug_renderer, xOff, yOff + decoded_mode->lines, "%c", named_channels[j]);
			}
		}

		// draw working buffers
		if (debug_drawBuffersType >= 3) {
			if (debug_drawBuffersType >= 4)
				yOff = decoded_mode->lines + padding;

			const char* named_components_Empty[NUM_WORK_BUFFERS] = {"", "", "", ""};
			const char* named_components_Monochrome[NUM_WORK_BUFFERS] = {"Value", "", "", ""};
			const char* named_components_RGB[NUM_WORK_BUFFERS] = {"Red", "Green", "Blue", "Alpha"};
			const char* named_components_YRYRB[NUM_WORK_BUFFERS] = {"Luma", "Chroma (Red difference)", "Chroma (Blue difference)", "Alpha"};

			const char** named_components = nullptr;

			switch (decoded_mode->scan_type) {
				case SSTV::ScanType::Monochrome:
				case SSTV::ScanType::Sweep:
					named_components = &named_components_Monochrome[0];
					break;
				case SSTV::ScanType::RGB:
					named_components = &named_components_RGB[0];
					break;
				case SSTV::ScanType::YRYBY:
					named_components = &named_components_YRYRB[0];
					break;
				default:
					named_components = &named_components_Empty[0];
					break;
			}

			for (int i = 0; i < modePixelCount * NUM_WORK_BUFFERS; i += NUM_WORK_BUFFERS) {
				int idxLocal = i / NUM_WORK_BUFFERS;

				int x = idxLocal % decoded_mode->width;
				int y = idxLocal / decoded_mode->width;

				float* workVal = &work_buf[i];

				for (int j = 0; j < NUM_WORK_BUFFERS; j++) {
					std::uint8_t workPix = std::clamp<std::uint8_t>(workVal[j] * 255, 0, 255);

					xOff = (decoded_mode->width + padding) * (j+1);

					SDL_SetRenderDrawColor(debug_renderer, workPix, workPix, workPix, 255);
					SDL_RenderPoint(debug_renderer, xOff + x, yOff + y);
				}
			}

			for(int i = 0; i < NUM_WORK_BUFFERS; i++) {
				xOff = (decoded_mode->width + padding) * (i+1);

				SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 255);
				SDL_RenderDebugTextFormat(debug_renderer, xOff, yOff + decoded_mode->lines, "[Work%d] %s", i, named_components[i]);
			}
		}
	}

	void SSTVDecode::debug_DrawDecodingProgress() const {
		if (!SDL_WasInit(0) || debug_drawDecodingType <= 0)
			return;

		constexpr int arounds = 6;

		int lineWidthMax = 0;
		for (int i = decoding_instruction_idx - arounds; i < (ssize_t)decoding_instructions.size(); i++) {
			if (i >= 0 && decoding_instructions[i].name.length() > lineWidthMax)
				lineWidthMax = decoding_instructions[i].name.length();
		}

		int indexWidthNum = decoding_instruction_idx > 0 ? log10(decoding_instruction_idx) : 1;
		int indexWidth = indexWidthNum + 3; // number of chars in "[i] "
		int timePitchWidth = 4+2 + 3 + 4+2; // number of chars in "XXXXms @ XXXXHz"

		lineWidthMax += indexWidth;
		if (lineWidthMax < timePitchWidth)
			lineWidthMax = timePitchWidth;

		int lineHeight = (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 2) + 2;
		int lineWidth = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * (lineWidthMax + indexWidth);

		int textX = debug_windowDimensions[0] - lineWidth;
		int textY = (debug_windowDimensions[1] / 2) - (lineHeight * arounds);
		int textYOff = 0;

		// make sure this is a signed comparison!
		for (int i = decoding_instruction_idx - arounds; i < (ssize_t)decoding_instructions.size(); i++) {
			// early exit if less than zero
			if (i < 0) {
				textYOff += lineHeight;
				continue;
			}

			// stop processing after this
			if (i - decoding_instruction_idx > arounds)
				break;

			int alpha = 255 - ((std::fabs(i - decoding_instruction_idx) / (arounds + 0.5f)) * 255.f);
			SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, alpha);

			// draw a box around the current instruction
			if (i == decoding_instruction_idx) {
				SDL_FRect recty = {textX - 1, textY + textYOff - 2, lineWidth + 2, lineHeight + 1};
				SDL_RenderRect(debug_renderer, &recty);
			}

			const SSTV::Instruction* ins = &decoding_instructions[i];

			SDL_RenderDebugTextFormat(debug_renderer, textX, textY + textYOff, "[%d] %s", i, ins->name.c_str());

			if (ins->flags & SSTV::InstructionFlags::PitchIsDelegated)
				SDL_RenderDebugTextFormat(debug_renderer,
				textX + (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * (indexWidth + 1)),
					textY + textYOff + SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,
					"%.0fms param %.0f", ins->length_ms, ins->pitch);
			else
				SDL_RenderDebugTextFormat(debug_renderer,
					textX + (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * (indexWidth + 1)),
					textY + textYOff + SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,
					"%.0fms @ %.0fHz", ins->length_ms, ins->pitch);

			textYOff += lineHeight;
		}

		const int buffer_size = 320;

		float lineStorage = debug_GetScreenPosAtSample(decoding_state_storage[3]);
		float lineSampleBlockStart = debug_GetScreenPosAtSample(decoding_cur_sample - buffer_size);
		SDL_FRect lineSampleBlock { lineSampleBlockStart, 0, buffer_size / debug_graphFreqXScale, debug_windowDimensions[1]};

		SDL_SetRenderDrawColor(debug_renderer, 0, 255, 255, 255);
		SDL_RenderLine(debug_renderer, lineStorage, 0, lineStorage, debug_windowDimensions[1]);
		SDL_SetRenderDrawColor(debug_renderer, 0, 255, 0, 63);
		SDL_RenderFillRect(debug_renderer, &lineSampleBlock);
	}
#endif

	void SSTVDecode::ResetDecoding() {
		samples.clear();
		samples_freq.clear();
		has_started = false;
		is_done = false;
		has_decoded_image = false;

		decoding_cur_sample = -1;
		decoding_state = DecodingState::PreStart;
		memset(decoding_state_storage, 0, sizeof(decoding_state_storage));
		decoding_instructions.clear();
		decoding_instruction_last = nullptr;
		decoding_pos[0] = decoding_pos[1] = -1;
		decoding_instruction_idx = -1;
		decoding_highest_field_encountered = -1;

		decoded_mode = nullptr;
		decoded_mode_meta = nullptr;
		decoded_vis_code = 0;
		decoded_vis_parity = false;

		FreeBuffers();

		cordic_init();

#ifdef FASSTV_DEBUG
		debug_DebugWindowSetup();
#endif
	}

	bool SSTVDecode::GetModeFromDecodedVIS() {
		// try to get our mode
		decoded_mode = SSTV::GetMode(decoded_vis_code);

		if(decoded_mode != nullptr) {
			LogInfo("Read as VIS code {}, which is mode {}", decoded_vis_code, decoded_mode->name);
			if(expected_mode != nullptr && decoded_mode != expected_mode) {
				if(expected_mode_fallback) {
					LogInfo("That wasn't expected, falling back to mode {} and continuing...", decoded_vis_code, expected_mode->name);
					Decoding_SwitchState(DecodingState::FailureRecoverable);
					decoded_mode = expected_mode;
				} else {
					LogInfo("Mode {} wasn't our expected mode ({}). Exiting...", decoded_mode->name, expected_mode->name);
					Decoding_SwitchState(DecodingState::FailureCritical);
					is_done = true;
					return false;
				}
			}
		} else {
			LogInfo("Read as VIS code {}, which is not something we know. Exiting...", decoded_vis_code);
			Decoding_SwitchState(DecodingState::FailureCritical);
			is_done = true;
			return false;
		}

		// we're happy enough with this to get meta info
		decoded_mode_meta = SSTVMetadata::GetModeMetadata(decoded_mode);
		if(!decoded_mode_meta) {
			Decoding_SwitchState(DecodingState::FailureCritical);
			is_done = true;
			return false;
		}

		return true;
	}

	void SSTVDecode::BuildInstructionsAndBuffers() {
		auto& sstv = SSTV::The();

		// clear the instructions we had, and rebuild for the new mode
		sstv.CreateInstructions(decoding_instructions, decoded_mode, true, false);
		decoding_instruction_idx = 0;

		LogInfo("Rebuilt instructions for {}", decoded_mode->name);

		// alloc the working buffer (floats)
		work_buf_size = decoded_mode->width * decoded_mode->lines * sizeof(float) * NUM_WORK_BUFFERS;
		work_buf = static_cast<float*>(malloc(work_buf_size));
		memset(work_buf, 0, work_buf_size);

		// alloc the pixel buffer (RGB888)
		pixel_buf_size = decoded_mode->width * decoded_mode->lines * sizeof(std::uint8_t) * NUM_CHANNELS;
		pixel_buf = static_cast<std::uint8_t*>(malloc(pixel_buf_size));
		memset(pixel_buf, 0, pixel_buf_size);
	}

	void SSTVDecode::MakeImageFromWorkBuffer(int startX /*= 0*/, int startY /*= 0*/) {
		// make the working buffer into an image

		int maxX = std::min<int>(decoded_mode->width, decoding_pos[0]);
		int maxY = std::min<int>(decoded_mode->lines, decoding_pos[1]);

		if (startX < 0)
			startX = 0;
		if (startY < 0)
			startY = 0;

		for(int x = startX; x < maxX; x++) {
			for(int y = startY; y < maxY; y++) {
				float* work_val = &work_buf[((y * decoded_mode->width) + x) * NUM_WORK_BUFFERS];

				// if we never encountered an alpha channel, make alpha max
				if(decoding_highest_field_encountered < 3)
					work_val[3] = 1.f;

				std::uint8_t* pix = &pixel_buf[((y * decoded_mode->width) + x) * NUM_CHANNELS];

				std::uint8_t work_val_byte = std::clamp<std::uint8_t>(work_val[0] * 255, 0, 255);

				// worrying about checking this each time. slow?
				// todo: make a handler function for each
				switch(decoded_mode->scan_type) {
					case SSTV::ScanType::Monochrome:
					case SSTV::ScanType::Sweep: pix[0] = pix[1] = pix[2] = work_val_byte; break;
					case SSTV::ScanType::RGB:
						for(int i = 0; i < NUM_CHANNELS; i++)
							pix[i] = std::clamp<std::uint8_t>(work_val[i] * 255, 0, 255);
						break;
					case SSTV::ScanType::YRYBY:
						std::uint8_t YRYBY[NUM_CHANNELS];
						for(int i = 0; i < NUM_CHANNELS; i++)
							YRYBY[i] = std::clamp<std::uint8_t>(work_val[i] * 255, 0, 255);

						pix[0] = std::clamp(0.003906 * ((298.082 * (YRYBY[0] - 16.0)) + (408.583 * (YRYBY[1] - 128.0))), 0., 255.);
						pix[1] = std::clamp(0.003906 * ((298.082 * (YRYBY[0] - 16.0)) + (-100.291 * (YRYBY[2] - 128.0)) + (-208.12 * (YRYBY[1] - 128.0))), 0., 255.);
						pix[2] = std::clamp(0.003906 * ((298.082 * (YRYBY[0] - 16.0)) + (516.411 * (YRYBY[2] - 128.0))), 0., 255.);
						pix[3] = YRYBY[3];
					default:
						break;
				}
			}
		}
	}

	void SSTVDecode::Decoding_SwitchState(DecodingState state) {
		if (decoding_state == state)
			return;

		LogDebug("{} -> {}", magic_enum::enum_name(decoding_state), magic_enum::enum_name(state));
		decoding_state = state;
		// reset state storage
		memset(decoding_state_storage, 0, sizeof(decoding_state_storage));
	}

	void SSTVDecode::PumpDecoding(float* arr, size_t arr_len) {
		if (is_done)
			return;

		for (size_t i = 0; i < arr_len; i++) {
			samples.push_back(arr[i]);
			samples_freq.push_back(rolling_freq_from_sample(arr[i] * INT16_MAX, samplerate));
		}

		if (decoding_state < DecodingState::DecodingStateFirst || decoding_state > DecodingState::DecodingStateLast) {
			LogError("Decoding state is invalid! Reset decoder first.");
			return;
		}

		auto& sstv = SSTV::The();

		// The Big Boy
		// there's a lot of unfortunately duplicated code in here. but the specific ways each "duplicate" is used makes them unique enough to not want to change
		// there may be some worth in splitting these out into their own functions, however.

		// general essence of a loop is that <for each sample we just added> we check backwards and see if the instruction we're looking to fit latches in
		// if it does, great! set storage[3] to the ending sample, increment our instruction index, and move on to the next sample
		// if it doesn't, just move on to the next sample. this is kind of inefficient, but it makes it easier to handle potential failures due to stutters

		switch (decoding_state) {
			case DecodingState::PreStart: {
				has_started = true;
				Decoding_SwitchState(DecodingState::StartBuildInstructions);
				break;
			}
			case DecodingState::StartBuildInstructions: {
				sstv.CreateVISHeader(decoding_instructions, 0);
				decoding_instruction_idx = 2; // testing skipping the leader
				Decoding_SwitchState(DecodingState::StartTryAcquire);
				break;
			}
			case DecodingState::StartTryAcquire: {
				// todo: sync
				Decoding_SwitchState(DecodingState::StartTryAcquireByHeader);
				break;
			}
			case DecodingState::StartTryAcquireByHeader: {
				// storage[3]: delay checking until this sample

				for (int i = 0; i < arr_len; i++) {
					if (decoding_instruction_idx >= decoding_instructions.size())
						break;

					SSTV::Instruction* ins = &decoding_instructions[decoding_instruction_idx];
					int widthSamples = SecondsToSamples(ins->length_ms / 1000.f);

					int cur_sample = decoding_cur_sample + i;
					if (cur_sample < decoding_state_storage[3] + widthSamples)
						continue;

					if (ins->type == SSTV::InstructionType::VIS || ins->type == SSTV::InstructionType::VISBit)
						break;

					bool isHalved = ins->type != SSTV::InstructionType::Leader;

					int check_freqMargin = ins->type == SSTV::InstructionType::Leader ? 200 : 50;
					int check_widthSamples = isHalved ? widthSamples / 2 : widthSamples;
					int check_sample = cur_sample - (check_widthSamples / 2);

					bool hit = AverageFreqAtAreaExpected(check_sample, ins->pitch, check_freqMargin, 0.01f, check_widthSamples, nullptr, ins->name, false);

					if (hit) {
						LogDebug("Leader hit! {}", ins->name);

						// redo for logging purpose
						AverageFreqAtAreaExpected(check_sample, ins->pitch, check_freqMargin, 0.01f, check_widthSamples, nullptr, ins->name, true);

						decoding_state_storage[3] = cur_sample;
						decoding_instruction_idx++;
					}
				}

				// fall through if we hit a VIS
				if (decoding_instruction_idx < decoding_instructions.size()) {
					SSTV::Instruction* ins = &decoding_instructions[decoding_instruction_idx];
					if (!(ins->type == SSTV::InstructionType::VIS || ins->type == SSTV::InstructionType::VISBit))
						break;

					Decoding_SwitchState(DecodingState::StartTryReadVIS);
					decoding_state_storage[3] = decoding_cur_sample;
				}
				else {
					break;
				}
			}
			case DecodingState::StartTryReadVIS: {
				// storage[0]: cur vis bit
				// storage[3]: delay checking until this sample. passed in

				for (int i = 0; i < arr_len; i++) {
					if (decoding_instruction_idx >= decoding_instructions.size())
						break;

					SSTV::Instruction* ins = &decoding_instructions[decoding_instruction_idx];
					int widthSamples = SecondsToSamples(ins->length_ms / 1000.f);

					int cur_sample = decoding_cur_sample + i;
					if (cur_sample < decoding_state_storage[3] + widthSamples)
						continue;

					bool isVISBit = ins->type == SSTV::InstructionType::VISBit;

					int check_widthSamples = widthSamples;
					int check_sample = cur_sample - (check_widthSamples / 2);

					bool hit = AverageFreqAtAreaExpected(check_sample, ins->pitch, 50, 0.1f, check_widthSamples, nullptr, ins->name, false);

					if (hit || isVISBit) {
						if (hit && !isVISBit)
							LogDebug("VIS hit! {}", ins->name);

						// redo for logging purpose
						hit = AverageFreqAtAreaExpected(check_sample, ins->pitch, 50, 0.1f, check_widthSamples, nullptr, ins->name, true);

						if (isVISBit) {
							if (decoding_state_storage[0] < 7) {
								// vis bit

								if (!hit)
									decoded_vis_code = decoded_vis_code | static_cast<std::uint8_t>(1 << decoding_state_storage[0]);

								LogDebug("VIS bit {}, {}. VIS is now {}", decoding_state_storage[0], !hit, decoded_vis_code);

								decoding_state_storage[0]++;
							}
							else {
								// parity
								decoded_vis_parity = __builtin_parity(decoded_vis_code);

								if (decoded_vis_parity != !hit) {
									LogError("bit parity was wrong!");
									Decoding_SwitchState(DecodingState::FailureCritical);
									is_done = true;
									return;
								}
							}
						}

						decoding_state_storage[3] = cur_sample;
						decoding_instruction_idx++;
						decoding_instruction_last = ins;

						if (decoding_instruction_idx >= decoding_instructions.size()) {
							// we got to the end, break this loop and fall through
							break; // break the loop, not the case
						}
					}
				}

				// fall through if we're out of header stuff
				if (decoding_instruction_idx >= decoding_instructions.size()) {
					int temp = decoding_state_storage[3];
					Decoding_SwitchState(DecodingState::ScanSetup);
					decoding_state_storage[3] = temp;
					// fall through
				}
				else {
					break;
				}
			}
			case DecodingState::ScanSetup: {
				if(!GetModeFromDecodedVIS())
					return;

				BuildInstructionsAndBuffers();

				#ifdef FASSTV_DEBUG
				// we have a proper signal, show it on screen when hands are busy
				if (debug_drawBuffersType == 0)
					debug_drawBuffersType = 1;
				#endif

				int temp = decoding_state_storage[3];
				Decoding_SwitchState(DecodingState::ScanDoLines);
				decoding_state_storage[3] = temp;

				// fall through
			}
			case DecodingState::ScanDoLines: {
				// storage[0]: sample of last sync instruction
				// storage[3]: delay checking until this sample. passed in

				for (int i = 0; i < arr_len; i++) {
					if (decoding_instruction_idx >= decoding_instructions.size())
						break;

					SSTV::Instruction* ins = &decoding_instructions[decoding_instruction_idx];
					int widthSamples = SecondsToSamples(ins->length_ms / 1000.f);

					int cur_sample = decoding_cur_sample + i;
					if (cur_sample < decoding_state_storage[3] + widthSamples)
						continue;

					float expectedPitch = ins->pitch;
					if (ins->flags & SSTV::InstructionFlags::PitchUsesIndex) {
						expectedPitch = decoded_mode->frequencies[ins->pitch];
					}

					// increment if this is the first new line
					if (ins != decoding_instruction_last && ins->flags & SSTV::InstructionFlags::NewLine) {
						//decoding_state_storage[1] = decoding_instruction_idx;
						decoding_pos[1]++;
					}

					if (ins->type != SSTV::InstructionType::Sync) {
						if (ins->type == SSTV::InstructionType::Scan) {
							// read the whole scanline, I guess?
							// it would be cool to do this based on the samples we just added instead of waiting for end of line...

							int check_sample = cur_sample - widthSamples;

							int field = std::clamp<int>(ins->pitch, 0, NUM_WORK_BUFFERS);
							if (field > decoding_highest_field_encountered)
								decoding_highest_field_encountered = field;

							int section_width_in_samples = widthSamples / decoded_mode->width;

							// not always an integer number of samples, so let's increment by this
							float section_width_in_ms = ins->length_ms / decoded_mode->width;

							for (int j = 0; j < decoded_mode->width; j++) {
								decoding_pos[0] = j;
								float* work_val = &work_buf[((decoding_pos[1]*decoded_mode->width) + j) * NUM_WORK_BUFFERS];

								float freq = AverageFreqAtArea(check_sample + (((j * section_width_in_ms) / 1000.f) * (float)samplerate), section_width_in_samples, std::format("F{}_P{}", field, j));
								// normalize to 0.0-1.0
								// width of range is 2300-1500 = 800
								float freqAdj = (freq - 1500.f) / 800.f;

								// todo: put this behind an option
								freqAdj = std::clamp<float>(freqAdj, 0.f, 1.f);

								if (freq > 0) {
									if (ins->pitch != field)
										LogDebug("Scan field out of bounds for our working buffer");

									work_val[field] = freqAdj;

									if (ins->flags & SSTV::InstructionFlags::ScanIsDoubled && decoding_pos[1] < decoded_mode->lines - 1) {
										work_val = &work_buf[(((decoding_pos[1]+1)*decoded_mode->width) + j) * NUM_WORK_BUFFERS];
										work_val[field] = freqAdj;
									}
								}
							}

							#ifdef FASSTV_DEBUG
							MakeImageFromWorkBuffer(0, decoding_pos[1]-1);
							#endif
						}

						decoding_state_storage[3] = cur_sample;
						decoding_instruction_idx++;
					}
					else {
						int check_freqMargin = 100;
						int check_freqMarginLeniency = 0.5f;
						int check_widthSamples = widthSamples / 2;
						int check_sample = cur_sample - (check_widthSamples / 2);

						bool hit = AverageFreqAtAreaExpected(check_sample, expectedPitch, check_freqMargin, check_freqMarginLeniency, check_widthSamples, nullptr, ins->name, false);

						if (hit) {
							//LogDebug("Sync hit! {}", ins->name);

							// redo for logging purpose
							AverageFreqAtAreaExpected(check_sample, expectedPitch, check_freqMargin, check_freqMarginLeniency, check_widthSamples, nullptr, ins->name, true);

							if (decoding_state_storage[0] > 0) {
								int diff = cur_sample - decoding_state_storage[0];
								float diffSec = (diff / (float)samplerate);
								float metaSec = (decoded_mode_meta->sync_between_ms / 1000.f);

								//LogDebug("Diff: {}s (vs meta's {}s)", diffSec, metaSec);

								float diffMeta = fabs(diffSec - metaSec);
								if (diffMeta > metaSec * 0.05f) {
									LogError("Sync is greater than 5% off! ({:.0f}%)", diffMeta * 100.f);
									LogDebug("Diff: {}s (vs meta's {}s)", diffSec, metaSec);

									int frac = std::round(diffSec / metaSec);
									LogInfo("Skipping {} lines", frac);

									decoding_pos[1] = std::clamp<int>(decoding_pos[1] + frac, 0, decoded_mode->lines);
									decoding_instruction_idx += ((int)decoded_mode->instructions_looping.size() - decoded_mode->instruction_loop_start - 1) * frac;
								}
							}

							decoding_state_storage[0] = cur_sample;

							decoding_state_storage[3] = cur_sample;
							decoding_instruction_idx++;
						}
					}

					decoding_instruction_last = ins;
				}

				if (decoding_instruction_idx >= decoding_instructions.size()) {
					// we're done!
					Decoding_SwitchState(DecodingState::Finish);
				}

				break;
			}
			case DecodingState::Finish: {
				MakeImageFromWorkBuffer();
				is_done = true;
				has_decoded_image = true;
				break;
			}
			default: {
				LogDebug("Not handled yet {}", (int)decoding_state);
				break;
			}
		}

		decoding_cur_sample += arr_len;
	}

	void SSTVDecode::DecodeAllSamples(std::vector<float>& samples) {
		ResetDecoding();
		this->samples = samples;

		has_started = true;

		LogInfo("Getting frequencies...");

		// replace all samples with their estimated frequency (I simply don't care about it anymore)
		samples_freq.resize(samples.size());
		for (int i = 0; i < samples.size(); i++)
			samples_freq[i] = rolling_freq_from_sample(samples[i] * INT16_MAX, samplerate);

		auto& sstv = SSTV::The();

		sstv.CreateVOXHeader(decoding_instructions);
		const int instVISStart = 8; //decoding_instructions.size();
		sstv.CreateVISHeader(decoding_instructions, 0);
		const int instVISEnd = 21; //decoding_instructions.size();

		int fudge_smp = 35; // fudge factor for frequency checking. accounts for the delay from the filter

		int progress_smp = 0 + fudge_smp;

		// let's do VIS and VOX
		// check for the VIS code, then run CreateInstructions with the mode we figure it is
		// start the next instructions at instVISEnd

		LogInfo("Reading header...");
		Decoding_SwitchState(DecodingState::StartTryAcquireByHeader);

		for (int i = 0; i < decoding_instructions.size(); i++) {
			auto& ins = decoding_instructions[i];

			int width_samples = (ins.length_ms / 1000.f) * samplerate;
			float center = progress_smp + (width_samples / 2);
			float back = 0.f;

			if (i < instVISStart) {
				//LogDebug("Ins {} tracking at {}ms", ins.name, center);
				AverageFreqAtAreaExpected(center, ins.pitch, 30.f, 1.f, width_samples / 2, &back, ins.name);
			}
			else if (i >= instVISStart + 3 && i < instVISEnd) {
				// we're in vis, let's read it!
				Decoding_SwitchState(DecodingState::StartTryReadVIS);

				int vis_idx = i - instVISStart - 3;

				//LogDebug("Ins {}, vis idx {}", ins.name, vis_idx);

				if (vis_idx == 0 || vis_idx == 9) {
					// start/end
					bool inRange = AverageFreqAtAreaExpected(center, ins.pitch, 30.f, 1.f, width_samples / 2, &back, ins.name);

					if (!inRange)
						LogError("oops, what we thought was VIS didn't start/end properly (wrong pitch, {} vs expected {})", back, ins.pitch);
				}
				else if (vis_idx > 0 && vis_idx < 8) {
					// put the vis code together
					std::uint8_t bit = vis_idx - 1;
					bool bitOn = AverageFreqAtAreaExpected(center, SSTV::The().VIS_BIT_FREQS[1], 30.f, 1.f, width_samples / 2, &back, ins.name);

					if (bitOn)
						decoded_vis_code = decoded_vis_code | static_cast<std::uint8_t>(1 << bit);

					//LogDebug("VIS bit {}, {}. VIS is now {}", bit, bitOn, decoded_vis_code);
				}
				else if (vis_idx == 8) {
					// check parity
					// oh god this may be GCC only?
					decoded_vis_parity = __builtin_parity(decoded_vis_code);

					bool bitOn = AverageFreqAtAreaExpected(center, SSTV::The().VIS_BIT_FREQS[1], 30.f, 1.f, width_samples / 2, &back, ins.name);

					if (decoded_vis_parity != bitOn) {
						LogError("bit parity was wrong!");
						Decoding_SwitchState(DecodingState::FailureCritical);
						is_done = true;
						return;
					}
				}
			}

			decoding_instruction_idx = i;
			progress_smp += SecondsToSamples(ins.length_ms / 1000.f);
		}

		if(!GetModeFromDecodedVIS())
			return;

		BuildInstructionsAndBuffers();

		// we have our mode, time for real instructions!
		Decoding_SwitchState(DecodingState::ScanDoLines);
		int cur_line = -1;

		int loopEnd = decoding_instructions.size();
		//loopEnd = (ourMode->instructions_looping.size() - ourMode->instruction_loop_start) * 32;
		for (int i = 0; i < loopEnd; i++) {
			auto& ins = decoding_instructions[i];

			int width_samples = (ins.length_ms / 1000.f) * samplerate;
			float center = progress_smp + (width_samples / 2.f);

			// LogDebug("Ins {} tracking at {}ms", ins.name, center);

			float back = 0.f;

			float expectedPitch = ins.pitch;
			if (ins.flags & SSTV::InstructionFlags::PitchUsesIndex) {
				expectedPitch = decoded_mode->frequencies[ins.pitch];
			}
			else if (ins.flags & SSTV::InstructionFlags::PitchIsDelegated) {
				// likely a scan
				expectedPitch = (2300-1500)/2 + 1500;
			}

			//AverageFreqAtAreaExpected(center, expectedPitch, 50.f, 1.f, width_samples, &back, ins.name);

			if (ins.flags & SSTV::InstructionFlags::NewLine)
				cur_line++;

			if (ins.type != SSTV::InstructionType::Scan) {
				AverageFreqAtAreaExpected(center, expectedPitch, ins.type == SSTV::InstructionType::Sync ? 200.f : 40.f, 1.f, width_samples, &back, ins.name);
			}
			else {
				AverageFreqAtAreaExpected(center, 1900.f, 800.f, 1.f, width_samples, nullptr, ins.name);

				int field = std::clamp<int>(ins.pitch, 0, NUM_WORK_BUFFERS);
				if (field > decoding_highest_field_encountered)
					decoding_highest_field_encountered = field;

				int section_width_in_samples = width_samples / decoded_mode->width;

				// not always an integer number of samples, so let's increment by this
				float section_width_in_ms = ins.length_ms / decoded_mode->width;

				for (int j = 0; j < decoded_mode->width; j++) {
					float* work_val = &work_buf[((cur_line*decoded_mode->width) + j) * NUM_WORK_BUFFERS];

					float freq = AverageFreqAtArea(progress_smp + (((j * section_width_in_ms) / 1000.f) * (float)samplerate), section_width_in_samples, std::format("F{}_P{}", field, j));
					// normalize to 0.0-1.0
					// width of range is 2300-1500 = 800
					float freqAdj = (freq - 1500.f) / 800.f;

					// todo: put this behind an option
					freqAdj = std::clamp<float>(freqAdj, 0.f, 1.f);

					if (freq > 0) {
						if (ins.pitch != field)
							LogDebug("Scan field out of bounds for our working buffer");

						work_val[field] = freqAdj;

						if (ins.flags & SSTV::InstructionFlags::ScanIsDoubled && cur_line < decoded_mode->lines - 1) {
							work_val = &work_buf[(((cur_line+1)*decoded_mode->width) + j) * NUM_WORK_BUFFERS];
							work_val[field] = freqAdj;
						}
					}
				}
			}

			decoding_instruction_idx = i;
			progress_smp += width_samples;
		}

		LogInfo("Done reading!");
		Decoding_SwitchState(DecodingState::Finish);

		MakeImageFromWorkBuffer();
		has_decoded_image = true;

		is_done = true;
	}

	void SSTVDecode::FreeBuffers() {
		if (work_buf != nullptr) {
			free(work_buf);
			work_buf = nullptr;
			work_buf_size = 0;
		}

		if (pixel_buf != nullptr) {
			free(pixel_buf);
			pixel_buf = nullptr;
			pixel_buf_size = 0;
		}

#ifdef FASSTV_DEBUG
		debug_AverageFreqInfo.clear();
#endif
	}

	std::uint8_t* SSTVDecode::GetPixels(size_t* out_size) const {
		if (!has_started)
			return nullptr;

		if (out_size)
			*out_size = pixel_buf_size;

		return pixel_buf;
	}

} // namespace fasstv