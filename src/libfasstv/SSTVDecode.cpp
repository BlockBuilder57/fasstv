// Created by block on 2025-12-08.

#include <libfasstv/SSTV.hpp>
#include <libfasstv/SSTVDecode.hpp>
#include <util/Logger.hpp>
#include "../../third_party/PicoSSTV/cordic.h"
#include "../../third_party/PicoSSTV/half_band_filter2.h"

#include <cstring>

#ifdef FASSTV_DEBUG
#include <SDL3/SDL.h>
#endif

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

namespace fasstv {

	SSTVDecode& SSTVDecode::The() {
		static SSTVDecode the;
		return the;
	}

	SSTVDecode::~SSTVDecode() {
		FreeBuffers();
	}

	const float fudge_ms = 6.f; // fudge factor for frequency checking. accounts for the delay from the filter

	float SSTVDecode::AverageFreqAtArea(float pos_ms, int width_samples /*= 10*/) {
		if (width_samples <= 1) {
			// just get the sample at pos_ms
			return samples_freq[(pos_ms / 1000.f) * samplerate];
		}

		pos_ms += fudge_ms;

		int rangeCenter = (pos_ms / 1000.f) * samplerate;
		int rangeMin = rangeCenter - (width_samples / 2);
		int rangeMax = rangeCenter + (width_samples / 2);

		rangeMin = std::clamp(rangeMin, 0, (int)samples_freq.size() - 1);
		rangeMax = std::clamp(rangeMax, 0, (int)samples_freq.size() - 1);

		//LogDebug("AverageFreqAtArea checking {}-{}", rangeMin, rangeMax);

		std::vector<float> samplesToAverage;
		samplesToAverage.resize(rangeMax - rangeMin);

		std::copy(&samples_freq[rangeMin], &samples_freq[rangeMax], samplesToAverage.data());

		float avg = 0;
		for (int i = 0; i < samplesToAverage.size(); i++) {
			avg += samplesToAverage[i];
		}
		avg /= samplesToAverage.size();

		return avg;
	}

	bool SSTVDecode::AverageFreqAtAreaExpected(float pos_ms, float freq_expected, float freq_margin /*= 50.f*/, int width_samples /*= 10*/, float* freq_back /*= nullptr*/) {
		float avg = AverageFreqAtArea(pos_ms, width_samples);

		if (freq_back)
			*freq_back = avg;

		bool tooBig = avg > freq_expected + (freq_margin / 2);
		bool tooSmall = avg < freq_expected - (freq_margin / 2);

		if (tooBig || tooSmall)
			return false;

		return true;
	}

#ifdef FASSTV_DEBUG
	bool SSTVDecode::debug_AverageFreqAtAreaExpected(std::string_view text, float pos_ms, float freq_expected, float freq_margin /*= 50.f*/, int width_samples /*= 10*/, float* freq_back /*= nullptr*/) {
		// make a back if we don't have one
		float backer = 0;
		if (freq_back == nullptr)
			freq_back = &backer;

		bool res = AverageFreqAtAreaExpected(pos_ms, freq_expected, freq_margin, width_samples, freq_back);

		if (!SDL_WasInit(0))
			return res;

		// debug area
		float sampCenter = ((((pos_ms + fudge_ms) / 1000.f) * samplerate) / debug_graphFreqXScale) - ((debug_graphFreqXPos / debug_graphFreqXScale) * samplerate);
		float sampMin = sampCenter - (width_samples / debug_graphFreqXScale / 2);
		float sampMax = sampCenter + (width_samples / debug_graphFreqXScale / 2);

		int freqYExpected = debug_windowDimensions[1]-(freq_expected/debug_graphFreqYScale)+debug_graphFreqYPos;
		int freqYObserved = debug_windowDimensions[1]-(*freq_back/debug_graphFreqYScale)+debug_graphFreqYPos;
		int freqYMargin = (freq_margin / 2)/debug_graphFreqYScale;

		// draw expected line
		SDL_SetRenderDrawColor(debug_renderer, 255, 0, 255, 255);
		SDL_RenderLine(debug_renderer, sampMin, freqYExpected, sampMax, freqYExpected);
		// draw observed line
		SDL_SetRenderDrawColor(debug_renderer, 0, 0, 255, 255);
		SDL_RenderLine(debug_renderer, sampMin, freqYObserved, sampMax, freqYObserved);

		// draw rectangle
		if (res)
			SDL_SetRenderDrawColor(debug_renderer, 40, 127, 10, 255);
		else
			SDL_SetRenderDrawColor(debug_renderer, 255, 0, 0, 255);

		SDL_FRect rect {sampMin, freqYExpected - freqYMargin, sampMax - sampMin, freqYMargin * 2};
		SDL_RenderRect(debug_renderer, &rect);
		SDL_RenderDebugText(debug_renderer, sampMin, freqYExpected + freqYMargin + 1, text.data());

		return res;
	}

	float SSTVDecode::debug_AverageFreqAtArea(std::string_view text, float pos_ms, int width_samples /*= 10*/) {
		float avg = AverageFreqAtArea(pos_ms, width_samples);

		if (!SDL_WasInit(0))
			return avg;

		// debug area
		float sampCenter = ((((pos_ms + fudge_ms) / 1000.f) * samplerate) / debug_graphFreqXScale) - ((debug_graphFreqXPos / debug_graphFreqXScale) * samplerate);
		float sampMin = sampCenter - (width_samples / debug_graphFreqXScale / 2);
		float sampMax = sampCenter + (width_samples / debug_graphFreqXScale / 2);

		int freqYObserved = debug_windowDimensions[1]-(avg/debug_graphFreqYScale)+debug_graphFreqYPos;

		// draw width lines
		const int lineHeight = 5;
		SDL_SetRenderDrawColor(debug_renderer, 255, 0, 255, 255);
		SDL_RenderLine(debug_renderer, sampMin, freqYObserved - lineHeight, sampMin, freqYObserved + lineHeight);
		SDL_RenderLine(debug_renderer, sampMax, freqYObserved - lineHeight, sampMax, freqYObserved + lineHeight);
		SDL_RenderLine(debug_renderer, sampMax, freqYObserved - lineHeight, sampMax, freqYObserved + lineHeight);

		// draw observed
		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 0, 255);
		SDL_RenderLine(debug_renderer, sampMax, freqYObserved, sampMax, freqYObserved);

		//SDL_RenderDebugText(debug_renderer, sampMin, freqYExpected + freqYMargin + 1, text.data());

		return avg;
	}

	SDL_Renderer* SSTVDecode::debug_DebugWindowSetup() {
		if (!SDL_WasInit(0))
			return nullptr;

		SDL_Window* window;
		SDL_CreateWindowAndRenderer("fasstv decoding", debug_windowDimensions[0], debug_windowDimensions[1], 0, &window, &debug_renderer);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
		SDL_GL_SetSwapInterval(1);

		SDL_SetRenderDrawColor(debug_renderer, 0, 0, 0, 255);
		SDL_RenderClear(debug_renderer);
		SDL_RenderPresent(debug_renderer);

		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 255);
		return debug_renderer;
	}

	bool SSTVDecode::debug_DebugWindowPump(SDL_Event* ev) {
		if (!SDL_WasInit(0) || debug_renderer == nullptr)
			return false;



		if (ev->type == SDL_EVENT_KEY_DOWN) {
			float posShift = 0.05f * debug_graphFreqXScale; // seconds!
			if (ev->key.mod & SDL_KMOD_SHIFT)
				posShift = 1.0f * debug_graphFreqXScale;
			else if (ev->key.mod & SDL_KMOD_CTRL)
				posShift = 1.f / samplerate; // one sample

			switch (ev->key.scancode) {
				case SDL_SCANCODE_UP:
					debug_graphFreqXScale += ev->key.mod & SDL_KMOD_SHIFT ? debug_graphFreqXScale : 0.1f;
					break;
				case SDL_SCANCODE_DOWN:
					debug_graphFreqXScale -= ev->key.mod & SDL_KMOD_SHIFT ? debug_graphFreqXScale * 0.5f : 0.1f;
					break;
				case SDL_SCANCODE_LEFT:
					debug_graphFreqXPos -= posShift;
					break;
				case SDL_SCANCODE_RIGHT:
					debug_graphFreqXPos += posShift;
					break;
				case SDL_SCANCODE_0:
				case SDL_SCANCODE_HOME:
					debug_graphFreqXPos = 0;
					break;
				case SDL_SCANCODE_9:
				case SDL_SCANCODE_END:
					debug_graphFreqXPos = SamplesLengthInSeconds();
					break;
				case SDL_SCANCODE_1:
				case SDL_SCANCODE_2:
				case SDL_SCANCODE_3:
					int newMode = ev->key.scancode - (SDL_SCANCODE_1 - 1);
					debug_drawBuffersType = debug_drawBuffersType == newMode ? 0 : newMode;
					break;
			}
		}

		// clamp
		debug_graphFreqXPos = std::clamp(debug_graphFreqXPos, 0.f, SamplesLengthInSeconds());
		debug_graphFreqYPos = std::clamp(debug_graphFreqYPos, 0.f, 3000.f);

		return true;
	}

	void SSTVDecode::debug_DebugWindowRender() {
		if (!SDL_WasInit(0) || debug_renderer == nullptr)
			return;

		debug_DrawFrequencyGraph();
		debug_DrawBuffersToScreen();

		// draw pos and scale
		SDL_SetRenderDrawColor(debug_renderer, 127, 127, 127, 255);
		SDL_RenderDebugTextFormat(debug_renderer, 0, debug_windowDimensions[1]-8, "t: %.3fs (%d) | Scale: %.2f (%d samples)", debug_graphFreqXPos, (int)(debug_graphFreqXPos * samplerate), debug_graphFreqXScale, (int)(debug_graphFreqXScale * debug_windowDimensions[0]));

		SDL_RenderPresent(debug_renderer);
		SDL_SetRenderDrawColor(debug_renderer, 0, 0, 0, 255);
		SDL_RenderClear(debug_renderer);

		return;
	}

	void SSTVDecode::debug_DrawFrequencyReferenceLines() {
		if (!SDL_WasInit(0))
			return;

		SDL_SetRenderDrawColor(debug_renderer, 80, 80, 80, 255);
		const int reflines[7] = { 1100, 1200, 1300, 1500, 1900, 2100, 2300 };
		for(int num : reflines) {
			SDL_RenderDebugText(debug_renderer, 0, debug_windowDimensions[1] - (num / debug_graphFreqYScale) + debug_graphFreqYPos + 1, std::to_string(num).c_str());

			for(int i = 0; i < debug_windowDimensions[0]; i += 2)
				SDL_RenderPoint(debug_renderer, i, debug_windowDimensions[1] - (num / debug_graphFreqYScale) + debug_graphFreqYPos);
		}
	}

	void SSTVDecode::debug_DrawFrequencyGraph() {
		if (!SDL_WasInit(0))
			return;

		// debug samples and freq to screen
		int lastX = -1;
		const int start = debug_graphFreqXPos * samplerate;
		for(int i = start; i < samples.size(); i++) {
			float frequency = samples_freq[i];

			int curX = ((i / debug_graphFreqXScale) - (start / debug_graphFreqXScale));
			if(curX != lastX) {
				// draw samples
				// SDL_SetRenderDrawColor(debug_renderer, 127, 0, 0, 255);
				// SDL_RenderPoint(debug_renderer, curX, debug_windowDimensions[1] - ((samples[i] + 1.0f) * 0.5f) * debug_windowDimensions[1]);

				// freq
				SDL_SetRenderDrawColor(debug_renderer, 180, 180, 180, 255);
				SDL_RenderPoint(debug_renderer, curX, debug_windowDimensions[1] - (frequency / debug_graphFreqYScale) + debug_graphFreqYPos);
			}

			lastX = curX;

			// stop if we go off-screen
			if(curX > debug_windowDimensions[0])
				break;
		}

		debug_DrawFrequencyReferenceLines();
	}

	void SSTVDecode::debug_DrawBuffersToScreen() {
		if (!SDL_WasInit(0) || debug_drawBuffersType <= 0)
			return;

		int xOff = 0;
		int yOff = 0;

		int modePixelCount = ourMode->width * ourMode->lines;

		const int padding = 8;

		// draw pixels
		for (int i = 0; i < modePixelCount * NUM_CHANNELS; i += NUM_CHANNELS) {
			int idxLocal = i / NUM_CHANNELS;

			int x = idxLocal % ourMode->width;
			int y = idxLocal / ourMode->width;

			SDL_SetRenderDrawColor(debug_renderer, pixel_buf[i + 0], pixel_buf[i + 1], pixel_buf[i + 2], 255);
			SDL_RenderPoint(debug_renderer, xOff + x, yOff + y);

			if (debug_drawBuffersType > 1) {
				for (int j = 0; j < NUM_CHANNELS; j++) {
					xOff = (ourMode->width + padding) * (j+1);

					SDL_SetRenderDrawColor(debug_renderer, pixel_buf[i + j], pixel_buf[i + j], pixel_buf[i + j], 255);
					SDL_RenderPoint(debug_renderer, xOff + x, yOff + y);
				}

				xOff = 0;
			}
		}

		const char* named_channels = "RGB";
		SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 255);
		SDL_RenderDebugText(debug_renderer, xOff, yOff + ourMode->lines, named_channels);
		if (debug_drawBuffersType > 1) {
			for (int j = 0; j < NUM_CHANNELS; j++) {
				xOff = (ourMode->width + padding) * (j+1);
				SDL_RenderDebugTextFormat(debug_renderer, xOff, yOff + ourMode->lines, "%c", named_channels[j]);
			}
		}

		/*for(int i = 0; i < 4; i++) {
			xOff = (ourMode->width + padding) * i;

			for(int x = 0; x < ourMode->width; x++) {
				for(int y = 0; y < ourMode->lines; y++) {
					std::uint8_t* pix = &pixel_buf[((y * ourMode->width) + x) * NUM_CHANNELS];

					if(i == 0)
						SDL_SetRenderDrawColor(debug_renderer, pix[0], pix[1], pix[2], 255);
					else
						SDL_SetRenderDrawColor(debug_renderer, pix[i], pix[i], pix[i], 255);

					SDL_RenderPoint(debug_renderer, xOff + x, yOff + y);
				}
			}

			SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 255);
			SDL_RenderDebugTextFormat(debug_renderer, xOff, yOff + ourMode->lines, "%c", char_channel[i]);
		}*/

		// draw working buffers
		if (debug_drawBuffersType >= 3) {
			yOff = ourMode->lines + padding;

			for (int i = 0; i < modePixelCount * NUM_WORK_BUFFERS; i += NUM_WORK_BUFFERS) {
				int idxLocal = i / NUM_WORK_BUFFERS;

				int x = idxLocal % ourMode->width;
				int y = idxLocal / ourMode->width;

				std::uint8_t w_pix[NUM_WORK_BUFFERS];
				for (int j = 0; j < NUM_WORK_BUFFERS; j++) {
					w_pix[j] = std::clamp<std::uint8_t>(work_buf[i + j] * 255, 0, 255);

					xOff = (ourMode->width + padding) * (j+1);

					SDL_SetRenderDrawColor(debug_renderer, w_pix[j], w_pix[j], w_pix[j], 255);
					SDL_RenderPoint(debug_renderer, xOff + x, yOff + y);
				}
			}

			for(int i = 0; i < 3; i++) {
				xOff = (ourMode->width + padding) * (i+1);

				SDL_SetRenderDrawColor(debug_renderer, 255, 255, 255, 255);
				SDL_RenderDebugTextFormat(debug_renderer, xOff, yOff + ourMode->lines, "Work%d", i);
			}
		}
	}
#endif

	void SSTVDecode::DecodeSamples(std::vector<float>& samples, int samplerate, SSTV::Mode* expectedMode /*= nullptr*/) {
#ifdef FASSTV_DEBUG
		debug_DebugWindowSetup();
#endif

		//this->samples = samples;
		this->samples = samples;
		this->samplerate = samplerate;
		hasDecoded = false;

		FreeBuffers();

		cordic_init();

		ourMode = nullptr;
		samples_freq.clear();

		LogInfo("Getting frequencies...");

		// replace all samples with their estimated frequency (I simply don't care about it anymore)
		samples_freq.resize(samples.size());
		for (int i = 0; i < samples.size(); i++)
			samples_freq[i] = rolling_freq_from_sample(samples[i] * INT16_MAX, samplerate);

		auto sstv = SSTV::The();
		std::vector<SSTV::Instruction> instructions;

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

		LogInfo("Reading header...");

		for (int i = 0; i < instructions.size(); i++) {
			auto& ins = instructions[i];

			int width_samples = (ins.length_ms / 1000.f) * samplerate;
			float center = progress_ms + (ins.length_ms / 2.f);
			float back = 0.f;

			if (i < instVISStart) {
				//LogDebug("Ins {} tracking at {}ms", ins.name, center);
				bool res = AverageFreqAtAreaExpected(center, ins.pitch, 30.f, width_samples / 2, &back);
			}
			else if (i >= instVISStart + 3 && i < instVISEnd) {
				// we're in vis, let's read it!
				int vis_idx = i - instVISStart - 3;

				//LogDebug("Ins {}, vis idx {}", ins.name, vis_idx);

				if (vis_idx == 0 || vis_idx == 9) {
					// start/end
					bool inRange = AverageFreqAtAreaExpected(center, ins.pitch, 30.f, width_samples / 2, &back);

					if (!inRange)
						LogError("oops, what we thought was VIS didn't start/end properly (wrong pitch, {} vs expected {})", back, ins.pitch);
				}
				else if (vis_idx > 0 && vis_idx < 8) {
					// put the vis code together
					std::uint8_t bit = vis_idx - 1;
					bool bitOn = AverageFreqAtAreaExpected(center, SSTV::The().VIS_BIT_FREQS[1], 30.f, width_samples / 2, &back);

					if (bitOn)
						vis_code = vis_code | static_cast<std::uint8_t>(1 << bit);

					//LogDebug("VIS bit {}, {}. VIS is now {}", bit, bitOn, vis_code);
				}
				else if (vis_idx == 8) {
					// check parity
					// oh god this may be GCC only?
					vis_parity = __builtin_parity(vis_code);

					bool bitOn = AverageFreqAtAreaExpected(center, SSTV::The().VIS_BIT_FREQS[1], 30.f, width_samples / 2, &back);

					if (vis_parity != bitOn) {
						LogError("bit parity was wrong!");
						return;
					}
				}
			}

			progress_ms += ins.length_ms;
		}

		// try to get our mode
		ourMode = SSTV::GetMode(vis_code);

		if (ourMode != nullptr)
			LogInfo("Read as VIS code {}, which is mode {}. Assuming this...", vis_code, ourMode->name);
		else {
			LogInfo("Read as VIS code {}, which is not something we know. Exiting...", vis_code);
			return;
		}

		// clear the instructions we had, and rebuild for the new mode
		sstv.CreateInstructions(instructions, ourMode);

		LogInfo("Rebuilt instructions for {}", ourMode->name);

		// alloc the working buffer (floats)
		work_buf_size = ourMode->width * ourMode->lines * sizeof(float) * NUM_WORK_BUFFERS;
		work_buf = static_cast<float*>(malloc(work_buf_size));
		memset(work_buf, 0, work_buf_size);

		// alloc the pixel buffer (RGB888)
		pixel_buf_size = ourMode->width * ourMode->lines * sizeof(std::uint8_t) * NUM_CHANNELS;
		pixel_buf = static_cast<std::uint8_t*>(malloc(pixel_buf_size));
		memset(pixel_buf, 0, pixel_buf_size);

		int cur_line = -1;

		// we have our mode, time for real instructions!
		int loopEnd = instructions.size();
		//loopEnd = instVISEnd + (ourMode->instructions_looping.size() - ourMode->instruction_loop_start) * 32;
		for (int i = instVISEnd; i < loopEnd; i++) {
			auto& ins = instructions[i];

			float center = progress_ms + (ins.length_ms / 2.f);
			float back = 0.f;
			int width_samples = (ins.length_ms / 1000.f) * samplerate;
			int start_samples = (progress_ms / 1000.f) * samplerate;

			//LogDebug("Ins {} tracking at {}ms", ins.name, center);

			float expectedPitch = ins.pitch;
			if (ins.flags & SSTV::InstructionFlags::PitchUsesIndex) {
				expectedPitch = ourMode->frequencies[ins.pitch];
			}

			if (ins.flags & SSTV::InstructionFlags::NewLine)
				cur_line++;

			if (ins.type != SSTV::InstructionType::Scan) {
				bool res = AverageFreqAtAreaExpected(center, expectedPitch, 30.f, width_samples / 2, &back);
			}
			else {
				float width_sampleSection = ins.length_ms / ourMode->width;

				for (int j = 0; j < ourMode->width; j++) {
					float* work_val = &work_buf[((cur_line*ourMode->width) + j) * NUM_WORK_BUFFERS];

					float freq = AverageFreqAtArea(progress_ms + (j * width_sampleSection), (width_sampleSection / 1000.f) * samplerate);
					// normalize to 0.0-1.0
					// width of range is 2300-1500 = 800
					float freqAdj = (freq - 1500.f) / 800.f;

					int field = std::clamp<int>(ins.pitch, 0, 2);

					if (freq > 0) {
						if (ins.pitch != field)
							LogDebug("Scan field out of bounds for our working buffer");

						work_val[field] = freqAdj;

						if (ins.flags & SSTV::InstructionFlags::ScanIsDoubled && cur_line < ourMode->lines - 1) {
							work_val = &work_buf[(((cur_line+1)*ourMode->width) + j) * NUM_WORK_BUFFERS];
							work_val[field] = freqAdj;
						}
					}
				}
			}

			progress_ms += ins.length_ms;
		}

		LogInfo("Done reading!");

		// make the working buffer into an image

		for (int x = 0; x < ourMode->width; x++) {
			for (int y = 0; y < ourMode->lines; y++) {
				float* work_val = &work_buf[((y*ourMode->width) + x) * NUM_WORK_BUFFERS];
				std::uint8_t* pix = &pixel_buf[((y*ourMode->width) + x) * NUM_CHANNELS];

				std::uint8_t work_val_byte = std::clamp<std::uint8_t>(work_val[0] * 255, 0, 255);

				// worrying about checking this each time. slow?
				// todo: make a handler function for each
				switch (ourMode->scan_type) {
					case SSTV::ScanType::Monochrome:
					case SSTV::ScanType::Sweep:
						pix[0] = pix[1] = pix[2] = work_val_byte;
						break;
					case SSTV::ScanType::RGB:
						for (int i = 0; i < NUM_CHANNELS; i++)
							pix[i] = std::clamp<std::uint8_t>(work_val[i] * 255, 0, 255);
						break;
					case SSTV::ScanType::YRYBY:
						std::uint8_t YRYBY[3];
						for (int i = 0; i < NUM_CHANNELS; i++)
							YRYBY[i] = std::clamp<std::uint8_t>(work_val[i] * 255, 0, 255);

						pix[0] = 0.003906 * ((298.082 * (YRYBY[0] - 16.0)) + (408.583 *  (YRYBY[1] - 128.0)));
						pix[1] = 0.003906 * ((298.082 * (YRYBY[0] - 16.0)) + (-100.291 * (YRYBY[2] - 128.0)) + (-208.12 * (YRYBY[1] - 128.0)));
						pix[2] = 0.003906 * ((298.082 * (YRYBY[0] - 16.0)) + (516.411 *  (YRYBY[2] - 128.0)));
					default:
						break;
				}
			}
		}

		LogInfo("Assembled image!");

		hasDecoded = true;
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
	}
} // namespace fasstv