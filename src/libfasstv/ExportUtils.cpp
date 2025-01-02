// Created by block on 2024-01-02.

#include <libfasstv/ExportUtils.hpp>

#include <concepts>
#include <filesystem>
#include <fstream>
#include <memory.h>
#include <util/Logger.hpp>

namespace fasstv {

	void stream_add_str(std::ofstream& file, std::string_view str) {
		file.write(str.begin(), str.length());
	}

	template<typename T>
	concept arithmetic = std::integral<T> or std::floating_point<T>;

	template <typename T> requires arithmetic<T>
	void stream_add_num(std::ofstream& file, T num, bool flip = false) {
		char arr[sizeof(T)] = {};
		memcpy(&arr[0], &num, sizeof(T));

		if (flip) {
			char temp;
			for (std::uint32_t i = 0; i < sizeof(T)/2; i++) {
				std::uint32_t idx = sizeof(T)-1-i;
				//LogDebug("Swapping {} ({:02x}) and {} ({:02x})?", i, arr[i], idx, arr[idx]);
				temp = arr[i];
				arr[i] = arr[idx];
				arr[idx] = temp;
			}
		}

		file.write(&arr[0], sizeof(T));
	}

	bool SamplesToWAV(std::vector<float>& samples, int samplerate, std::ofstream& file) {
		std::streampos startPos = file.tellp();
		const int channels = 1;
		const int bitDepth = sizeof(float) * 8;

		// Header chunk
		//
		stream_add_str(file, "RIFF");

		// we'll come back to this
		int fileSize = 69; // @0x4

		stream_add_num<std::uint32_t>(file, fileSize);

		// WAVE chunk
		//
		stream_add_str(file, "WAVE");

		// fmt chunk
		//
		stream_add_str(file, "fmt ");
		stream_add_num<std::uint32_t>(file, 16); // chunk size
		stream_add_num<std::uint16_t>(file, 0x0003); // format type, IEEE float
		stream_add_num<std::uint16_t>(file, channels);
		stream_add_num<std::uint32_t>(file, samplerate);

		int bytesPerSec = (channels * samplerate * bitDepth) / 8;
		stream_add_num<std::uint32_t>(file, bytesPerSec);

		std::uint16_t blockAlign = channels * (bitDepth / 8);
		stream_add_num<std::uint16_t>(file, blockAlign);

		stream_add_num<std::uint16_t>(file, bitDepth);

		// fact chunk
		//
		stream_add_str(file, "fact");
		stream_add_num<std::uint32_t>(file, 4); // chunk size
		stream_add_num<std::uint32_t>(file, samples.size());

		// DATA chunk
		//
		stream_add_str(file, "data");
		stream_add_num(file, samples.size() * sizeof(float));
		for (float& smp : samples)
			stream_add_num(file, smp);


		// seek back for filesize
		fileSize = file.tellp();
		file.seekp((int)startPos + 4);
		stream_add_num<std::uint32_t>(file, fileSize - 8);

		return true;
	}

	bool SamplesToBIN(std::vector<float>& samples, std::ofstream& file) {
		for (float& smp : samples)
			stream_add_num(file, smp);

		return true;
	}

} // namespace fasstv
