// Created by block on 2025-12-08.

#include "fasstv-cli/DecodingTests.hpp"

#include <fftw3.h>
#include <SDL3/SDL.h>

#include <array>
#include <complex>
#include <util/Logger.hpp>

#include "../../third_party/PicoSSTV/cordic.h"
#include "../../third_party/PicoSSTV/half_band_filter2.h"

const float freqYScale = 2.f;
const float freqXScale = 1.f;
const float freqYStart = 1000.f / freqYScale; // in hertz
const float freqXStart = 4.6f; // in seconds

// Majority of frequency tracking code by Jon Dawson
// https://github.com/dawsonjon/PicoSSTV

half_band_filter2 ssb_filter;
uint8_t ssb_phase = 0;
int16_t last_phase = 0;

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
	return std::min(std::max(smoothed_freq_at_sample, 1000u), 2400u);
}

void ActualStuff(std::vector<float>& samples, int samplerate, SDL_Renderer* renderer, const int* dims) {
	cordic_init();

	int lastX = -1;
	const int start = freqXStart * samplerate;
	for(int i = start; i < samples.size(); i++) {
		int16_t frequency = rolling_freq_from_sample(samples[i] * INT16_MAX, samplerate);

		int curX = ((i / freqXScale) - (start / freqXScale));
		if(curX != lastX) {
			// draw samples
			SDL_SetRenderDrawColor(renderer, 127, 0, 0, 255);
			SDL_RenderPoint(renderer, curX, dims[1] - ((samples[i] + 1.0f) * 0.5f) * dims[1]);

			// freq
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
			SDL_RenderPoint(renderer, curX, dims[1] - (frequency / freqYScale) + freqYStart);
		}

		lastX = curX;
	}
}

void DecodingTests::DoTheThing(std::vector<float>& samples, int samplerate) {
	const int windowDimensions[2] = {2048, 768};
	SDL_Renderer* renderer;
	SDL_Window* window;
	SDL_CreateWindowAndRenderer("fasstv", windowDimensions[0], windowDimensions[1], 0, &window, &renderer);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

	ActualStuff(samples, samplerate, renderer, &windowDimensions[0]);

	SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
	const int reflines[7] = {1100, 1200, 1300, 1500, 1900, 2100, 2300};
	//const int reflines[1] = {2300};
	for (int num : reflines) {
		SDL_RenderDebugText(renderer, 0, windowDimensions[1]-(num/freqYScale)+freqYStart + 1, std::to_string(num).c_str());

		for (int i = 0; i < windowDimensions[0]; i+=2)
			SDL_RenderPoint(renderer, i, windowDimensions[1]-(num/freqYScale)+freqYStart);
	}

	//SDL_SetRenderDrawColor(renderer, 0, 80, 0, 255);
	//for (int i = 0; i < windowDimensions[0]; i+=4)
	//	for (int j = 0; j < windowSize; j++)
	//		SDL_RenderPoint(renderer, i, windowDimensions[1]-((j*binSizeInHertz)/freqYScale));

	SDL_RenderPresent(renderer);
}