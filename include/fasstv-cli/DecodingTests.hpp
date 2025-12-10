// Created by block on 2025-12-08.

#pragma once

#include <vector>

struct DecodingTests {
	static float AverageFreqAtArea(std::vector<float>& samples, int samplerate, int pos_ms, int width_samples = 10);

	static void DoTheThing(std::vector<float>& samples, int samplerate);
};
