// Created by block on 2025-12-14.

#include <libfasstv/SSTVMetadata.hpp>

#include <shared/Logger.hpp>

#include <math.h>

namespace fasstv {

	std::vector<SSTVMetadata::PerModeMetadata> SSTVMetadata::per_mode_metadata {};

	float SSTVMetadata::mode_longest_ms = 0.f;
	float SSTVMetadata::mode_shortest_ms = MAXFLOAT;

	SSTV::Mode* SSTVMetadata::mode_longest = nullptr;
	SSTV::Mode* SSTVMetadata::mode_shortest = nullptr;

	void SSTVMetadata::ProcessMetadata(SSTV::Mode* mode) {
		// almost a direct copy from SSTV::CreateInstructions

		std::vector<SSTV::Instruction> instructions;

		// some modes (for example, Robot 36) can define multiple lines per instruction set
		int instruction_divisor = 1;
		if (mode->uses_extra_lines) {
			instruction_divisor = 0;
			for (const SSTV::Instruction& ins : mode->instructions_looping) {
				if (ins.flags & SSTV::InstructionFlags::NewLine)
					instruction_divisor++;
			}
		}

		int lines = mode->lines / instruction_divisor;

		// add the first non-looping instructions
		if (mode->instruction_loop_start > 0) {
			// I don't think there's a need to worry about extra lines here
			for (int i = 0; i < mode->instruction_loop_start; i++)
				instructions.push_back(mode->instructions_looping[i]);
		}

		// check other looping instructions for length
		float loop_length_ms = 0.0f;
		for (int i = mode->instruction_loop_start; i < mode->instructions_looping.size(); i++) {
			auto& ins = mode->instructions_looping[i];

			float length_ms = ins.length_ms;

			if(ins.flags & SSTV::InstructionFlags::LengthUsesIndex)
				length_ms = mode->timings[ins.length_ms];

			loop_length_ms += length_ms;
		}
		// don't forget the multi-line things
		loop_length_ms /= instruction_divisor;

		for (int i = 0; i < lines; i++) {
			for (size_t j = mode->instruction_loop_start; j < mode->instructions_looping.size(); j++) {
				// found an extra line, but we don't use them - skip
				SSTV::Instruction ins = mode->instructions_looping[j];
				if (!mode->uses_extra_lines && ins.flags & SSTV::InstructionFlags::ExtraLine)
					continue;

				instructions.push_back(ins);
			}
		}

		float total_length_ms = 0.0f;

		for (auto& ins : instructions) {
			float length_ms = ins.length_ms;

			if(ins.flags & SSTV::InstructionFlags::LengthUsesIndex)
				length_ms = mode->timings[ins.length_ms];

			ins.length_ms = length_ms;
			total_length_ms += length_ms;
		}

		if (total_length_ms > mode_longest_ms) {
			mode_longest_ms = total_length_ms;
			mode_longest = mode;
		}
		if (total_length_ms < mode_shortest_ms) {
			mode_shortest_ms = total_length_ms;
			mode_shortest = mode;
		}

		//LogDebug("Mode {}", mode->name);
		//LogDebug("    Total length: {}s", total_length_ms / 1000.f);
		//LogDebug("    Loop length: {}s", loop_length_ms / 1000.f);

		per_mode_metadata.emplace_back(mode, total_length_ms, loop_length_ms, 0);
	}

	void SSTVMetadata::BuildMetadata() {
		for(auto& mode : SSTV::The().MODES) {
			ProcessMetadata(&mode);
		}

		LogDebug("Longest mode is {} at {}s", mode_longest ? mode_longest->name : "(null)", mode_longest_ms / 1000.f);
		LogDebug("Shortest mode is {} at {}s", mode_shortest ? mode_shortest->name : "(null)", mode_shortest_ms / 1000.f);
	}

	SSTVMetadata::PerModeMetadata* SSTVMetadata::GetModeMetadata(SSTV::Mode* mode) {
		for(auto& modemeta : per_mode_metadata) {
			if (modemeta.mode == mode)
				return &modemeta;
		}

		return nullptr;
	}

} // namespace fasstv