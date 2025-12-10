// Created by block on 2025-12-08.

#include <fasstv-cli/DecodingTests.hpp>
#include <util/Logger.hpp>
#include <libfasstv/SSTV.hpp>
#include "../../third_party/PicoSSTV/cordic.h"
#include "../../third_party/PicoSSTV/half_band_filter2.h"

#include <SDL3/SDL.h>

#include <array>
#include <complex>

const float freqYScale = 2.f;
const float freqXScale = 1.5f;
const float freqYStart = 1000.f / freqYScale; // in hertz
const float freqXStart = 1.4f; // in seconds

const float fudge_ms = 6.f; // fudge factor for frequency checking. accounts for the delay from the filter

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

float AverageFreqAtArea(std::vector<float>& samples, int samplerate, float pos_ms, int width_samples = 10) {
	pos_ms += fudge_ms;

	int rangeCenter = (pos_ms / 1000.f) * samplerate;
	int rangeMin = rangeCenter - (width_samples / 2);
	int rangeMax = rangeCenter + (width_samples / 2);

	//fasstv::LogDebug("AverageFreqAtArea checking {}-{}", rangeMin, rangeMax);

	std::vector<float> samplesToAverage;
	samplesToAverage.resize(rangeMax - rangeMin);

	std::copy(&samples[rangeMin], &samples[rangeMax], samplesToAverage.data());

	float avg = 0;
	for (int i = 0; i < samplesToAverage.size(); i++) {
		avg += samplesToAverage[i];
	}
	avg /= samplesToAverage.size();

	return avg;
}

bool AverageFreqAtAreaExpected(std::vector<float>& samples, int samplerate, float pos_ms, float freq_expected, float freq_margin = 50.f, int width_samples = 10, float* freq_back = nullptr) {
	float avg = AverageFreqAtArea(samples, samplerate, pos_ms, width_samples);

	if (freq_back)
		*freq_back = avg;

	bool tooBig = avg > freq_expected + (freq_margin / 2);
	bool tooSmall = avg < freq_expected - (freq_margin / 2);

	if (tooBig || tooSmall)
		return false;

	return true;
}

bool debug_AverageFreqAtAreaExpected(SDL_Renderer* renderer, fasstv::SSTV::Instruction* ins, std::vector<float>& samples, int samplerate, float pos_ms, float freq_expected, float freq_margin = 50.f, int width_samples = 10, float* freq_back = nullptr) {
	// make a back if we don't have one
	float backer = 0;
	if (freq_back == nullptr)
		freq_back = &backer;

	bool res = AverageFreqAtAreaExpected(samples, samplerate, pos_ms, freq_expected, freq_margin, width_samples, freq_back);

	int dims[2] = {};
	SDL_GetCurrentRenderOutputSize(renderer, &dims[0], &dims[1]);

	// debug area
	float sampCenter = ((((pos_ms + fudge_ms) / 1000.f) * samplerate) / freqXScale) - ((freqXStart / freqXScale) * samplerate);
	float sampMin = sampCenter - (width_samples / freqXScale / 2);
	float sampMax = sampCenter + (width_samples / freqXScale / 2);

	int freqYExpected = dims[1]-(freq_expected/freqYScale)+freqYStart;
	int freqYObserved = dims[1]-(*freq_back/freqYScale)+freqYStart;
	int freqYMargin = (freq_margin / 2)/freqYScale;

	// draw expected line
	SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
	SDL_RenderLine(renderer, sampMin, freqYExpected, sampMax, freqYExpected);
	// draw observed line
	SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
	SDL_RenderLine(renderer, sampMin, freqYObserved, sampMax, freqYObserved);

	// draw rectangle
	if (res)
		SDL_SetRenderDrawColor(renderer, 40, 127, 10, 255);
	else
		SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);

	SDL_FRect rect {sampMin, freqYExpected - freqYMargin, sampMax - sampMin, freqYMargin * 2};
	SDL_RenderRect(renderer, &rect);
	SDL_RenderDebugText(renderer, sampMin, freqYExpected + freqYMargin + 1, ins->name.c_str());

	return res;
}

void ActualStuff(std::vector<float>& samples, int samplerate, SDL_Renderer* renderer) {
	cordic_init();

	int dims[2] = {};
	SDL_GetCurrentRenderOutputSize(renderer, &dims[0], &dims[1]);

	// debug samples and freq to screen
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
			SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
			SDL_RenderPoint(renderer, curX, dims[1] - (frequency / freqYScale) + freqYStart);
		}

		lastX = curX;

		// stop if we go off-screen
		if (curX > dims[0])
			break;
	}

	// replace all samples with their estimated frequency (I simply don't care about it anymore)
	for (int i = 0; i < samples.size(); i++)
		samples[i] = rolling_freq_from_sample(samples[i] * INT16_MAX, samplerate);

	auto sstv = fasstv::SSTV::The();
	std::vector<fasstv::SSTV::Instruction> instructions;
	sstv.CreateVOXHeader(instructions);
	int instVISStart = instructions.size();
	sstv.CreateVISHeader(instructions, 0);
	int instVISEnd = instructions.size();

	float progress_ms = 0.f;

	// let's do VIS and VOX
	// check for the VIS code, then run CreateInstructions with the mode we figure it is
	// start the next instructions at instVISEnd
	std::uint8_t vis_code = 0;
	bool vis_parity = false;

	for (int i = 0; i < instructions.size(); i++) {
		auto& ins = instructions[i];

		int width_samples = (ins.length_ms / 1000.f) * samplerate;
		float center = progress_ms + (ins.length_ms / 2.f);
		float back = 0.f;

		if (i < instVISStart) {
			//fasstv::LogDebug("Ins {} tracking at {}ms", ins.name, center);
			bool res = debug_AverageFreqAtAreaExpected(renderer, &ins, samples, samplerate, center, ins.pitch, 30.f, width_samples / 2, &back);
		}
		else if (i >= instVISStart + 3 && i < instVISEnd) {
			// we're in vis, let's read it!
			int vis_idx = i - instVISStart - 3;

			//fasstv::LogDebug("Ins {}, vis idx {}", ins.name, vis_idx);

			if (vis_idx == 0 || vis_idx == 9) {
				// start/end
				bool inRange = debug_AverageFreqAtAreaExpected(renderer, &ins, samples, samplerate, center, ins.pitch, 30.f, width_samples / 2, &back);

				if (!inRange)
					fasstv::LogDebug("oops, what we thought was VIS didn't start/end properly (wrong pitch, {} vs expected {})", back, ins.pitch);
			}
			else if (vis_idx > 0 && vis_idx < 8) {
				// put the vis code together
				std::uint8_t bit = vis_idx - 1;
				bool bitOn = debug_AverageFreqAtAreaExpected(renderer, &ins, samples, samplerate, center, fasstv::SSTV::The().VIS_BIT_FREQS[1], 30.f, width_samples / 2, &back);

				if (bitOn)
					vis_code = vis_code | static_cast<std::uint8_t>(1 << bit);

				fasstv::LogDebug("VIS bit {}, {}. VIS is now {}", bit, bitOn, vis_code);
			}
			else if (vis_idx == 8) {
				// check parity
				// oh god this may be GCC only?
				vis_parity = __builtin_parity(vis_code);

				bool bitOn = debug_AverageFreqAtAreaExpected(renderer, &ins, samples, samplerate, center, fasstv::SSTV::The().VIS_BIT_FREQS[1], 30.f, width_samples / 2, &back);

				fasstv::LogDebug("read as VIS {}, which is mode {}", vis_code, fasstv::SSTV::GetMode(vis_code)->name);

				if (vis_parity != bitOn)
					fasstv::LogWarning("bit parity was wrong!");
			}
		}

		progress_ms += ins.length_ms;
	}

	// time for real instructions
	for (int i = instVISEnd; i < instructions.size(); i++) {
		auto& ins = instructions[i];

		float center = progress_ms + (ins.length_ms / 2.f);
		float back = 0.f;
		int width_samples = (ins.length_ms / 1000.f) * samplerate;

		//fasstv::LogDebug("Ins {} tracking at {}ms", ins.name, center);

		if (ins.type != fasstv::SSTV::InstructionType::Scan) {
			bool res = debug_AverageFreqAtAreaExpected(renderer, &ins, samples, samplerate, center, ins.pitch, 30.f, width_samples / 2, &back);
		}

		progress_ms += ins.length_ms;
	}
}

void DecodingTests::DoTheThing(std::vector<float>& samples, int samplerate) {
	const int windowDimensions[2] = {2048, 768};
	SDL_Renderer* renderer;
	SDL_Window* window;
	SDL_CreateWindowAndRenderer("fasstv", windowDimensions[0], windowDimensions[1], 0, &window, &renderer);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

	ActualStuff(samples, samplerate, renderer);

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