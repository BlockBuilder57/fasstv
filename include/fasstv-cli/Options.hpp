// Created by block on 2025-12-15.

#pragma once

#include <libfasstv/libfasstv.hpp>
#include <shared/ImageUtilities.hpp>

#include <filesystem>
#include <string>

namespace fasstv::cli {

	struct ScaleMethod {
		std::string name;
		int flags;
	};

	static struct ScaleMethod ScaleMethods[] = {
		{"fast_bilinear", SWS_FAST_BILINEAR},
		{"bilinear", SWS_FAST_BILINEAR},
		{"bicubic", SWS_BICUBIC},
		{"experimental", SWS_X},
		{"nearest", SWS_POINT},
		{"neighbor", SWS_POINT},
		{"point", SWS_POINT},
		{"area", SWS_AREA},
		{"bicublin", SWS_BICUBLIN},
		{"gauss", SWS_GAUSS},
		{"sinc", SWS_SINC},
		{"lanczos", SWS_LANCZOS},
		{"spline", SWS_SPLINE},
	};

	enum class FASSTVMode {
		Encode,
		Decode,
		Transcode,
		Invalid
	};

	struct OptionVariables {
		std::filesystem::path inputPath {};
		std::filesystem::path outputPath {};
		SSTV::Mode* mode = nullptr;
		float volume = 0.33f;
		bool play = false;

		FASSTVMode fasstv_mode = FASSTVMode::Invalid;

		struct EncodeOptions {
			int samplerate = 8000;
			bool separate_scans = false;

			std::string camera {};
			int camera_mode = 0;

			bool image_stretch = false;
			int image_resize_method = SWS_BICUBIC;

			float noise_strength = 0.f;
		} encode;

		struct DecodeOptions {
			std::string microphone {};
		} decode;

		struct TranscodeOptions {
			bool resize_mode_to_image = false;
		} transcode;
	};

	class Options {
	public:
		static OptionVariables options;

		static int ParseArgs(int argc, char** argv);
		static void PrintArgs();
	};


}