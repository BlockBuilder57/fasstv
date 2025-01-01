// Created by block on 2025-01-01.

#pragma once

#include <util/Logger.hpp>
#include <util/Rect.hpp>

#include <libfasstv/SSTV.hpp>

namespace fasstv {

	class SSTVEncode {
	   public:
		static SSTVEncode& The();

		typedef std::uint8_t* (*PixelProviderCallback)(int sample_x, int sample_y);

		void SetMode(const std::string_view& name);
		void SetMode(int vis_code);
		void SetMode(SSTV::Mode* mode);

		void SetSampleRate(int samplerate);
		void SetLetterbox(Rect rect);
		void SetLetterboxLines(bool b);
		void SetPixelProvider(PixelProviderCallback cb);
		void SetInstructionFlagMask(SSTV::InstructionFlags flags, bool invert);

		SSTV::Mode* GetMode();
		void GetState(std::int32_t* cur_x, std::int32_t* cur_y, std::uint32_t* cur_sample, std::uint32_t* length_in_samples);

		bool IsProcessingDone() const { return last_instruction_sample >= estimated_length_in_samples; }

		void ResetInstructionProcessing();
		void PumpInstructionProcessing(float* arr, size_t arr_size, Rect rect);
		void RunAllInstructions(std::vector<float>& samples, Rect rect);

		static float ScanSweep(SSTV::Mode* mode, int pos_x, bool invert);
		static float ScanMonochrome(SSTV::Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel);
		static float ScanRGB(SSTV::Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel);
		static float ScanYRYBY(SSTV::Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel);

	   private:
		bool GetNextInstruction();
		float GetSamplePitch(Rect rect);

		std::uint32_t samplerate = 44100;
		float timestep = 1.f / samplerate;
		std::uint32_t estimated_length_in_samples = 0;

		SSTV::Mode* current_mode = nullptr;
		SSTV::Instruction* current_instruction = nullptr;
		float phase = 0;

		std::vector<SSTV::Instruction> instructions {};
		std::vector<float> samples {};
		std::int32_t cur_x = -1;
		std::int32_t cur_y = -1;
		std::uint32_t cur_sample = 0;
		std::uint32_t last_instruction_sample = 0;

		bool letterboxLines = false;
		bool flag_mask_invert = false;
		Rect letterbox {};
		SSTV::InstructionFlags flag_mask {};
		PixelProviderCallback pixProviderFunc {};

	};

} // namespace fasstv
