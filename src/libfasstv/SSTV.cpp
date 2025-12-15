// Created by block on 5/25/24.

#include <libfasstv/SSTV.hpp>
#include <util/Logger.hpp>

#include <cmath>
#include <fstream>

namespace fasstv {

	SSTV& SSTV::The() {
		static SSTV the;
		return the;
	}

	SSTV::SSTV() {
		//SetMode("Robot 36");
	}

	SSTV::Mode* SSTV::GetMode(const std::string_view& name) {
		for (auto& mode : SSTV::The().MODES) {
			if (mode.name != name)
				continue;

			return &mode;
		}

		return nullptr;
	}

	SSTV::Mode* SSTV::GetMode(int vis_code) {
		for (auto& mode : SSTV::The().MODES) {
			if (mode.vis_code != vis_code)
				continue;

			return &mode;
		}

		return nullptr;
	}

	void SSTV::CreateInstructions(std::vector<Instruction>& instructions, const Mode* mode, bool clear /*= true*/) {
		if (clear)
			instructions.clear();

		CreateVOXHeader(instructions);
		SSTV::CreateVISHeader(instructions, mode->vis_code);

		// some modes (for example, Robot 36) can define multiple lines per instruction set
		int instruction_divisor = 1;
		if (mode->uses_extra_lines) {
			instruction_divisor = 0;
			for (const Instruction& ins : mode->instructions_looping) {
				if (ins.flags & InstructionFlags::NewLine)
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

		for (int i = 0; i < lines; i++) {
			for (size_t j = mode->instruction_loop_start; j < mode->instructions_looping.size(); j++) {
				// found an extra line, but we don't use them - skip
				SSTV::Instruction ins = mode->instructions_looping[j];
				if (!mode->uses_extra_lines && ins.flags & SSTV::InstructionFlags::ExtraLine)
					continue;

				instructions.push_back(ins);
			}
		}

		CreateFooter(instructions);

		// Set instruction length ahead of time
		for (auto& ins : instructions) {
			float length_ms = ins.length_ms;
			if(ins.flags & InstructionFlags::LengthUsesIndex) {
				length_ms = mode->timings[ins.length_ms];
				//LogDebug("Length from index: {}, {}", ins.length_ms, mode->timings[ins.length_ms]);
			}
			ins.length_ms = length_ms;
		}
	}

	void SSTV::CreateVOXHeader(std::vector<Instruction>& instructions) {
		instructions.push_back({"VOX Low",  The().VOX_LENGTH_MS, The().VOX_FREQS[1], VOX});
		instructions.push_back({"VOX Low",  The().VOX_LENGTH_MS, The().VOX_FREQS[0], VOX});
		instructions.push_back({"VOX Low",  The().VOX_LENGTH_MS, The().VOX_FREQS[1], VOX});
		instructions.push_back({"VOX Low",  The().VOX_LENGTH_MS, The().VOX_FREQS[0], VOX});
		instructions.push_back({"VOX High", The().VOX_LENGTH_MS, The().VOX_FREQS[2], VOX});
		instructions.push_back({"VOX High", The().VOX_LENGTH_MS, The().VOX_FREQS[0], VOX});
		instructions.push_back({"VOX High", The().VOX_LENGTH_MS, The().VOX_FREQS[2], VOX});
		instructions.push_back({"VOX High", The().VOX_LENGTH_MS, The().VOX_FREQS[0], VOX});
	}

	void SSTV::CreateVISHeader(std::vector<Instruction>& instructions, std::uint8_t vis_code) {
		instructions.push_back({"Leader 1",   The().VIS_LENGTHS_MS[2], The().VIS_FREQS[1], VIS});
		instructions.push_back({"break",      The().VIS_LENGTHS_MS[0],  The().VIS_FREQS[0], VIS});
		instructions.push_back({"Leader 2",   The().VIS_LENGTHS_MS[2], The().VIS_FREQS[1], VIS});

		// VIS
		instructions.push_back({"VIS start",  The().VIS_LENGTHS_MS[1],  The().VIS_FREQS[0], VIS});
		bool parity = false;
		for (int i = 0; i < 7; i++) {
			bool bit = vis_code & (1 << i);
			instructions.push_back({"VIS bit " + std::to_string(i), The().VIS_LENGTHS_MS[1], The().VIS_BIT_FREQS[bit], VIS});

			if (bit)
				parity = !parity;
		}
		instructions.push_back({"VIS parity", The().VIS_LENGTHS_MS[1], The().VIS_BIT_FREQS[parity], VIS});
		instructions.push_back({"VIS stop",   The().VIS_LENGTHS_MS[1],  The().VIS_FREQS[0], VIS});
	}

	void SSTV::CreateFooter(std::vector<Instruction>& instructions) {
		// I've got no clue if this is right. I could barely find information
		// on the VOX tones, and none about this. If I understand it right,
		// this may just be an MMSSTV feature?
		instructions.push_back({"Footer 1", The().VOX_LENGTH_MS, The().VOX_FREQS[1], VOX});
		instructions.push_back({"Footer 2", The().VOX_LENGTH_MS, The().VOX_FREQS[0], VOX});
		instructions.push_back({"Footer 3", The().VOX_LENGTH_MS, The().VOX_FREQS[1], VOX});
		instructions.push_back({"Footer 4", The().VOX_LENGTH_MS, The().VOX_FREQS[0], VOX});
	}

} // namespace fasstv
