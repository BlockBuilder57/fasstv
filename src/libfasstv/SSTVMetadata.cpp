// Created by block on 2025-12-14.

#include <math.h>
#include <unistd.h>

#include <libfasstv/SSTVMetadata.hpp>
#include <shared/Logger.hpp>

namespace fasstv {

	std::vector<SSTVMetadata::PerModeMetadata> SSTVMetadata::per_mode_metadata {};

	float SSTVMetadata::mode_longest_ms = 0.f;
	float SSTVMetadata::mode_shortest_ms = MAXFLOAT;
	float SSTVMetadata::mode_shortest_sync_ms = MAXFLOAT;
	float SSTVMetadata::mode_shortest_between_sync_ms = MAXFLOAT;

	SSTV::Mode* SSTVMetadata::mode_longest = nullptr;
	SSTV::Mode* SSTVMetadata::mode_shortest = nullptr;
	SSTV::Mode* SSTVMetadata::mode_shortest_sync = nullptr;
	SSTV::Mode* SSTVMetadata::mode_shortest_between_sync = nullptr;

	void SSTVMetadata::ProcessMetadata(SSTV::Mode* mode) {
		// almost a direct copy from SSTV::CreateInstructions

		std::vector<SSTV::Instruction> instructions;

		// some modes (for example, Robot 36) can define multiple lines per instruction set
		int lines = mode->lines / mode->instruction_loop_num_lines;

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
		loop_length_ms /= mode->instruction_loop_num_lines;

		for (int i = 0; i < lines; i++) {
			for (size_t j = mode->instruction_loop_start; j < mode->instructions_looping.size(); j++) {
				// found an extra line, but we don't use them - skip
				SSTV::Instruction ins = mode->instructions_looping[j];
				if (mode->instruction_loop_num_lines <= 1 && ins.flags & SSTV::InstructionFlags::ExtraLine)
					continue;

				instructions.push_back(ins);
			}
		}

		float total_length_ms = 0.0f;
		float scan_total_length_ms = 0.0f;
		float sync_between_ms = 0.0f;
		float last_sync = 0.0f;
		float sync_length_ms = 0.0f;
		int newline_interval = 0;
		int newline_interval_last = 0;

		for (int i = 0; i < instructions.size(); i++) {
			auto& ins = instructions[i];
			float length_ms = ins.length_ms;

			if (ins.flags & SSTV::InstructionFlags::LengthUsesIndex)
				length_ms = mode->timings[ins.length_ms];

			if (ins.flags & SSTV::InstructionFlags::NewLine && !(ins.flags & SSTV::InstructionFlags::ExtraLine)) {
				newline_interval = i - newline_interval_last;
				newline_interval_last = i;
			}

			if (ins.type == SSTV::InstructionType::Scan)
				scan_total_length_ms += length_ms;
			else if (ins.type == SSTV::InstructionType::Sync) {
				sync_length_ms = length_ms;

				if (last_sync <= 0)
					last_sync = total_length_ms;
				else if (sync_between_ms <= 0)
					sync_between_ms = total_length_ms - last_sync;
			}

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
		if (sync_length_ms < mode_shortest_sync_ms) {
			mode_shortest_sync_ms = sync_length_ms;
			mode_shortest_sync = mode;
		}
		if (sync_between_ms < mode_shortest_between_sync_ms) {
			mode_shortest_between_sync_ms = sync_between_ms;
			mode_shortest_between_sync = mode;
		}

		//LogDebug("Mode {}", mode->name);
		//LogDebug("    Total length: {}s", total_length_ms / 1000.f);
		//LogDebug("    Loop length: {}s", loop_length_ms / 1000.f);
		//LogDebug("    Scan total length: {}s ({:.2f}%)", scan_total_length_ms / 1000.f, (scan_total_length_ms / total_length_ms) * 100.f);
		//LogDebug("    Sync between: {}s", sync_between_ms / 1000.f);
		//LogDebug("    Sync length: {}s", sync_length_ms / 1000.f);
		//LogDebug("    New line interval: {} instructions", newline_interval);

		per_mode_metadata.emplace_back(mode, total_length_ms, loop_length_ms, scan_total_length_ms, sync_between_ms, sync_length_ms, newline_interval);
	}

	void SSTVMetadata::BuildMetadata() {
		for(auto& mode : SSTV::The().MODES) {
			ProcessMetadata(&mode);
		}

		//LogDebug("Longest mode is {} at {}s", mode_longest ? mode_longest->name : "(null)", mode_longest_ms / 1000.f);
		//LogDebug("Shortest mode is {} at {}s", mode_shortest ? mode_shortest->name : "(null)", mode_shortest_ms / 1000.f);
		//LogDebug("Shortest syncing mode is {} at {}s", mode_shortest_sync ? mode_shortest_sync->name : "(null)", mode_shortest_sync_ms / 1000.f);
	}

	SSTVMetadata::PerModeMetadata* SSTVMetadata::GetModeMetadata(SSTV::Mode* mode) {
		for(auto& modemeta : per_mode_metadata) {
			if (modemeta.mode == mode)
				return &modemeta;
		}

		return nullptr;
	}

} // namespace fasstv