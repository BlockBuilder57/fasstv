// Created by block on 2025-01-02.

#pragma once

#include <filesystem>
#include <fstream>
#include <vector>

namespace fasstv {

	bool SamplesToWAV(std::vector<float>& samples, int samplerate, std::ofstream& file);
	bool SamplesToBIN(std::vector<float>& samples, std::ofstream& file);

} // namespace fasstv
