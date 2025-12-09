// Created by block on 2025-12-08.

#include <util/Logger.hpp>
#include "fasstv-cli/DecodingTests.hpp"

#include <fftw3.h>
#include <SDL3/SDL.h>

#include <array>
#include <complex>

void hannWindow(std::vector<double>& samples) {
	// https://stackoverflow.com/a/3555393
	for (int i = 0; i < samples.size(); i++) {
		double multiplier = 0.5 * (1 - cos(2*M_PI*i/(samples.size()-1)));
		samples[i] = multiplier * samples[i];
	}
}

// Truthfully I don't know where this comes from, but the math checks out as reasonable
// Referenced from https://dspguru.com/dsp/howtos/how-to-interpolate-fft-peak/
float peak_interp(fftw_complex* out, int windowSize, int bestMagIdx) {
	int i1 = bestMagIdx <= 0 ? bestMagIdx : bestMagIdx - 1;
	int i2 = bestMagIdx;
	int i3 = bestMagIdx >= windowSize - 1 ? bestMagIdx : bestMagIdx + 1;

	fasstv::LogDebug("reasonable neighbors? {} {} {}", i1, i2, i3);

	float y1 = fabs(reinterpret_cast<std::complex<double>*>(out[i1])->imag());
	float y2 = fabs(reinterpret_cast<std::complex<double>*>(out[i2])->imag());
	float y3 = fabs(reinterpret_cast<std::complex<double>*>(out[i3])->imag());

	float denom = y3 + y2 + y1; // barycentric
	//float denom = (2 * (2 * y2 - y1 - y3)); // quadratic
	if (denom == 0)
		return 0;

	return ((y3 - y1) / denom) + bestMagIdx;
};



void DecodingTests::DoTheThing(std::vector<float>& samples, int samplerate) {
	const int windowDimensions[2] = {2048, 768};
	SDL_Renderer* renderer;
	SDL_Window* window;
	SDL_CreateWindowAndRenderer("fasstv", windowDimensions[0], windowDimensions[1], 0, &window, &renderer);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

	int windowSize = samplerate * 0.002;
	int line = 0;
	const int lineSpread = 4;
	const float freqScale = 6.f;
	float binSizeInHertz = samplerate/windowSize;

	fasstv::LogDebug("bin size: {}Hz", binSizeInHertz);

	fftw_complex* out;
	fftw_plan p;

	std::vector<double> doubleSamples {};
	doubleSamples.resize(windowSize);

	out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * windowSize);
	p = fftw_plan_dft_r2c_1d(windowSize, doubleSamples.data(), out, FFTW_ESTIMATE);

	for (int i = 0; i + windowSize < samples.size(); i += windowSize) {
		for (int j = 0; j < windowSize; j++)
			doubleSamples[j] = samples[i+j];

		hannWindow(doubleSamples);

		fftw_execute(p);

		float bestMag = -1;
		int bestMagIdx = -1;

		for (int j = 0; j < windowSize; j++) {
			auto thing = reinterpret_cast<std::complex<double>*>(out[j]);
			if (fabs(thing->imag()) >= bestMag) {
				bestMag = fabs(thing->imag());
				bestMagIdx = j;
			}
		}

		fasstv::LogDebug("best bin? {}", bestMagIdx);

		float peak = peak_interp(out, windowSize, bestMagIdx);
		//float peak = bestMagIdx;
		float inHertz = peak * samplerate / windowSize;

		fasstv::LogDebug("peak bin {}, {}Hz?", peak, inHertz);

		for (int j = 0; j < windowSize; j++) {
			auto thing = reinterpret_cast<std::complex<double>*>(out[j]);
			double val = thing->imag();
			std::uint8_t col = fabs(val) * 64;

			//fasstv::LogDebug("{}", thing->real());

			float binHertzCenter = j * samplerate / windowSize;

			if (val >= 0)
				SDL_SetRenderDrawColor(renderer, col, col, col, 255);
			else
				SDL_SetRenderDrawColor(renderer, col/4, col/4, col, 255);

			for (int k = 0; k < lineSpread; k++)
				SDL_RenderPoint(renderer, line+k, windowDimensions[1]-(binHertzCenter / freqScale));
		}

		SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
		for (int k = 0; k < lineSpread; k++)
			SDL_RenderPoint(renderer, line+k, windowDimensions[1]-(inHertz / freqScale));

		line += lineSpread;
		if (line > windowDimensions[0])
			break;
		//SDL_RenderPresent(renderer);
	}

	SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
	//const int reflines[3] = {1100, 2300, 1500};
	const int reflines[1] = {2300};
	for (int num : reflines) {
		for (int i = 0; i < windowDimensions[0]; i+=2)
			SDL_RenderPoint(renderer, i, windowDimensions[1]-(num/freqScale));
	}

	//SDL_SetRenderDrawColor(renderer, 0, 80, 0, 255);
	//for (int i = 0; i < windowDimensions[0]; i+=4)
	//	for (int j = 0; j < windowSize; j++)
	//		SDL_RenderPoint(renderer, i, windowDimensions[1]-((j*binSizeInHertz)/freqScale));

	SDL_RenderPresent(renderer);

	fftw_destroy_plan(p);
	fftw_free(out);
}