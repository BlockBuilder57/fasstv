// Created by block on 2024-11-14.

#pragma once

#include <filesystem>
#include <fstream>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace fasstv {

	bool SamplesToAVCodec(std::vector<float>& samples, int samplerate, std::ofstream& file, AVCodecID format = AV_CODEC_ID_MP3, int bit_rate = 320000);
	bool SamplesToWAV(std::vector<float>& samples, int samplerate, std::ofstream& file);
	bool SamplesToBIN(std::vector<float>& samples, std::ofstream& file);

} // namespace fasstv
