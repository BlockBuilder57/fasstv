// Created by block on 5/25/24.

#include <libfasstv/SSTV.hpp>

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

	void SSTV::CreateVOXHeader(std::vector<Instruction>& instructions) {
		instructions.push_back({"VOX Low",  100, 1900});
		instructions.push_back({"VOX Low",  100, 1500});
		instructions.push_back({"VOX Low",  100, 1900});
		instructions.push_back({"VOX Low",  100, 1500});
		instructions.push_back({"VOX High", 100, 2300});
		instructions.push_back({"VOX High", 100, 1500});
		instructions.push_back({"VOX High", 100, 2300});
		instructions.push_back({"VOX High", 100, 1500});
	}

	void SSTV::CreateVISHeader(std::vector<Instruction>& instructions, std::uint8_t vis_code) {
		instructions.push_back({"Leader 1",   300, 1900});
		instructions.push_back({"break",      10,  1200});
		instructions.push_back({"Leader 2",   300, 1900});

		// VIS
		instructions.push_back({"VIS start",  30,  1200});
		bool parity = false;
		for (int i = 0; i < 7; i++) {
			bool bit = vis_code & (1 << i);
			instructions.push_back({"VIS bit " + std::to_string(i), 30, bit ? 1100.f : 1300.f});

			if (bit)
				parity = !parity;
		}
		instructions.push_back({"VIS parity", 30, parity ? 1100.f : 1300.f});
		instructions.push_back({"VIS stop",   30,  1200});
	}

	void SSTV::CreateFooter(std::vector<Instruction>& instructions) {
		// I've got no clue if this is right. I could barely find information
		// on the VOX tones, and none about this. If I understand it right,
		// this may just be an MMSSTV feature?
		instructions.push_back({"Footer 1", 100, 1900});
		instructions.push_back({"Footer 2", 100, 1500});
		instructions.push_back({"Footer 3", 100, 1900});
		instructions.push_back({"Footer 4", 100, 1500});
	}

} // namespace fasstv
