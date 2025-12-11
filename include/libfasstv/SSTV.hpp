// Created by block on 5/25/24.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fasstv {

	class SSTV {
	   public:
		static SSTV& The();

		enum InstructionFlags : std::uint8_t {
			ExtraLine        = 0b0000001, // for lines that would be considered "extra"
			NewLine          = 0b0000010, // indicates the start of a new line
			LengthUsesIndex  = 0b0000100, // indicates that the length uses an index value in the mode
			PitchUsesIndex   = 0b0001000, // indicates that the pitch uses an index value in the mode
			PitchIsDelegated = 0b0010000, // indicates that the pitch is delegated to a scan handler
			PitchIsSweep     = 0b0100000,  // indicates that the pitch is a simple sweep (SSTV::ScanSweep)
			ScanIsDoubled    = 0b1000000  // indicates that the scan goes across two lines
		};

		enum InstructionType : std::uint8_t {
			InvalidInstructionType,
			VOX,
			VIS,
			Pulse,
			Porch,
			Scan,
			Any
		};

		enum ScanType : std::uint8_t {
			InvalidScanType,
			Monochrome,
			YRYBY, // also YCrCb
			RGB,
			Sweep
		};

		struct Instruction {
			std::string name {};
			float length_ms {};
			float pitch {}; // pitch or scan identifier
			InstructionType type {};
			InstructionFlags flags {};
		};

		struct Mode {
			std::string name;
			std::uint8_t vis_code;
			ScanType scan_type;
			std::uint16_t width;
			std::uint16_t lines;
			bool uses_extra_lines;
			std::vector<float> timings;
			std::vector<int> frequencies;
			std::vector<Instruction> instructions_looping;
			int instruction_loop_start;
		};

		const float VOX_FREQS[3] {1500, 1900, 2300}; // low, mid, high
		const float VOX_LENGTH_MS = 100; // length per instruction
		const float VIS_FREQS[2] {1200, 1900}; // break, leader
		const float VIS_BIT_FREQS[2] {1100, 1300}; // 0, 1
		const float VIS_LENGTHS_MS[3] = {10, 30, 300};

		std::vector<Instruction> ROBOT_4_2_0_INSTRUCTIONS = {
			{ "(1) Sync pulse",               0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Sync porch",               1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Y scan",                   2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) \"Even\" separator pulse", 3, 1, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Porch",                    4, 2, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(6) R-Y scan",                 5, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated | ScanIsDoubled) },
			{ "(7) Sync pulse",               0, 0, Pulse, (InstructionFlags)(ExtraLine | NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(8) Sync porch",               1, 1, Porch, (InstructionFlags)(ExtraLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(9) Y scan",                   2, 0, Scan, (InstructionFlags)(ExtraLine | LengthUsesIndex | PitchIsDelegated) },
			{ "(10) \"Odd\" separator pulse", 3, 3, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(11) Porch",                   4, 2, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(12) B-Y scan",                5, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated | ScanIsDoubled) },
		};

		std::vector<Instruction> ROBOT_4_2_2_INSTRUCTIONS = {
			{ "(1) Sync pulse",      0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Sync porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Y scan",          2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Separator pulse", 3, 1, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Porch",           4, 2, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(6) R-Y scan",        5, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(7) Separator pulse", 3, 3, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(8) Porch",           4, 2, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(9) B-Y scan",        5, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> ROBOT_MONOCHROME_INSTRUCTIONS = {
			{ "(1) Sync pulse", 0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Scan",       1, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> MARTIN_INSTRUCTIONS = {
			{ "(1) Sync pulse",      0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Sync porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Green scan",      2, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Separator pulse", 1, 1, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Blue scan",       2, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(6) Separator pulse", 1, 1, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(7) Red scan",        2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(8) Separator pulse", 1, 1, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) }
		};

		std::vector<Instruction> WRASSE_INSTRUCTIONS = {
			{ "(1) Sync pulse", 0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Green scan", 2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Blue scan",  2, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(5) Red scan",   2, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};


		std::vector<Instruction> SCOTTIE_INSTRUCTIONS = {
			{ "(1) \"Starting\" sync pulse ", 0, 0, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			//
			{ "(2) Separator pulse",          1, 1, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Green scan",               2, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Separator pulse",          1, 1, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Blue scan",                2, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(6) Sync pulse",               0, 0, Pulse, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(6) Sync porch",               1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(7) Red scan",                 2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> PD_INSTRUCTIONS = {
			{ "(1) Sync pulse",              0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Porch",                   1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Y scan (from odd line)",  2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) R-Y scan",                2, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated | ScanIsDoubled) },
			{ "(5) B-Y scan",                2, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated | ScanIsDoubled) },
			{ "(6) Y scan (from even line)", 2, 0, Scan, (InstructionFlags)(ExtraLine | NewLine | LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> PASOKON_INSTRUCTIONS = {
			{ "(1) Sync pulse", 0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Red scan",   2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Green scan", 2, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(6) Porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(7) Blue scan",  2, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(8) Porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) }
		};

		std::vector<Instruction> BLOCK_INSTRUCTIONS = {
			{ "(1) Sync pulse", 0, 0, Pulse, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Porch",      1, 1, Porch, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Red scan",   2, 0, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Green scan", 2, 1, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(5) Blue scan",  2, 2, Scan, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Mode> MODES = {
			// Robot
			{ "Robot 12", 0, ScanType::YRYBY,
			  160, 120, true,
			  {7.0f, 3.0f, 60.0f, 4.5f, 1.5f, 30.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  ROBOT_4_2_0_INSTRUCTIONS, 0
			},
			{ "Robot 24", 4, ScanType::YRYBY,
			  160, 120, false,
			  {9.0f, 3.0f, 88.0f, 4.5f, 1.5f, 44.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  ROBOT_4_2_2_INSTRUCTIONS, 0
			},
			{ "Robot 36", 8, ScanType::YRYBY,
			  320, 240, true,
			  {9.0f, 3.0f, 88.0f, 4.5f, 1.5f, 44.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  ROBOT_4_2_0_INSTRUCTIONS, 0
			},
			{ "Robot 72", 12, ScanType::YRYBY,
			  320, 240, false,
			  {9.0f, 3.0f, 138.0f, 4.5f, 1.5f, 69.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  ROBOT_4_2_2_INSTRUCTIONS, 0
			},
			{ "B&W 8", 2, ScanType::Monochrome,
			  160, 120, false,
			  {10.0f, 56.0f}, // sync, scan
			  {1200}, // sync
			  ROBOT_MONOCHROME_INSTRUCTIONS, 0
			},
			{ "B&W 12", 6, ScanType::Monochrome,
			  160, 120, false,
			  {7.0f, 93.0f}, // sync, scan
			  {1200}, // sync
			  ROBOT_MONOCHROME_INSTRUCTIONS, 0
			},
			{ "B&W 24", 10, ScanType::Monochrome,
			  320, 240, false,
			  {12.0f, 93.0f}, // sync, scan
			  {1200}, // sync
			  ROBOT_MONOCHROME_INSTRUCTIONS, 0
			},
			{ "B&W 36", 14, ScanType::Monochrome,
			  320, 240, false,
			  {12.0f, 138.0f}, // sync, scan
			  {1200}, // sync
			  ROBOT_MONOCHROME_INSTRUCTIONS, 0
			},

			// Martin
			{ "Martin 1", 44, ScanType::RGB,
			  320, 256, false,
			  {4.862f, 0.572f, 146.432f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  MARTIN_INSTRUCTIONS, 0
			},
			{ "Martin 2", 40, ScanType::RGB,
			  320, 256, false,
			  {4.862f, 0.572f, 73.216f}, // pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  MARTIN_INSTRUCTIONS, 0
			},
			{ "Martin 3", 36, ScanType::RGB,
			  128, 256, false,
			  {4.862f, 0.572f, 146.432f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  MARTIN_INSTRUCTIONS, 0
			},
			{ "Martin 4", 32, ScanType::RGB,
			  128, 256, false,
			  {4.862f, 0.572f, 73.216f}, // pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  MARTIN_INSTRUCTIONS, 0
			},

			// Wraase
			{ "Wraase SC2-180", 55, ScanType::RGB,
			  320, 256, false,
			  {5.5225f, 0.500f, 235.000f}, // pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  WRASSE_INSTRUCTIONS, 0
			},

			// Scottie
			{ "Scottie 1", 60, ScanType::RGB,
			  320, 256, false,
			  {9.0f, 1.5f, 138.240f}, // sync pulse, separator pulse
			  {1200, 1500}, // sync pulse, separator pulse
			  SCOTTIE_INSTRUCTIONS, 1
			},
			{ "Scottie 2", 56, ScanType::RGB,
			  320, 256, false,
			  {9.0f, 1.5f, 88.064f}, // sync pulse, separator pulse
			  {1200, 1500}, // sync pulse, separator pulse
			  SCOTTIE_INSTRUCTIONS, 1
			},
			{ "Scottie DX", 76, ScanType::RGB,
			  320, 256, false,
			  {9.0f, 1.5f, 345.6f}, // sync pulse, separator pulse
			  {1200, 1500}, // sync pulse, separator pulse
			  SCOTTIE_INSTRUCTIONS, 1
			},

			// PD
			{ "PD50", 93, ScanType::YRYBY,
			  320, 256, true,
			  {20.000f, 2.080f, 91.520f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  PD_INSTRUCTIONS, 0
			},
			{ "PD90", 99, ScanType::YRYBY,
			  320, 256, true,
			  {20.000f, 2.080f, 170.240f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  PD_INSTRUCTIONS, 0
			},
			{ "PD120", 95, ScanType::YRYBY,
			  640, 496, true,
			  {20.000f, 2.080f, 121.600f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  PD_INSTRUCTIONS, 0
			},
			{ "PD160", 98, ScanType::YRYBY,
			  512, 400, true,
			  {20.000f, 2.080f, 195.584f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  PD_INSTRUCTIONS, 0
			},
			{ "PD180", 96, ScanType::YRYBY,
			  640, 496, true,
			  {20.000f, 2.080f, 183.040f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  PD_INSTRUCTIONS, 0
			},
			{ "PD240", 97, ScanType::YRYBY,
			  640, 496, true,
			  {20.000f, 2.080f, 244.480f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  PD_INSTRUCTIONS, 0
			},
			{ "PD290", 94, ScanType::YRYBY,
			  800, 616, true,
			  {20.000f, 2.080f, 228.800f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  PD_INSTRUCTIONS, 0
			},

			// Pasokon
			{ "Pasokon P3", 113, ScanType::RGB,
			  640, 496, false,
			  {5.208f, 1.042f, 133.333f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  PASOKON_INSTRUCTIONS, 0
			},
			{ "Pasokon P5", 114, ScanType::RGB,
			  640, 496, false,
			  {7.813f, 1.563f, 200.000f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  PASOKON_INSTRUCTIONS, 0
			},
			{ "Pasokon P7", 115, ScanType::RGB,
			  640, 496, false,
			  {10.417f, 1.042f, 266.666f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  PASOKON_INSTRUCTIONS, 0
			},

			// Custom Things
			{ "Block57", 57, ScanType::YRYBY,
			  426, 240, false,
			{2.f, 0.5f, 100.f}, // pulse, porch, color scan
			{1200, 1500}, // sync pulse, porch
			  BLOCK_INSTRUCTIONS, 0
			},
		};

		SSTV();

		static Mode* GetMode(const std::string_view& name);
		static Mode* GetMode(int vis_code);

		static void CreateInstructions(std::vector<Instruction>& instructions, const Mode* mode, bool clear = true);
		static void CreateVOXHeader(std::vector<Instruction>& instructions);
		static void CreateVISHeader(std::vector<Instruction>& instructions, std::uint8_t vis_code);
		static void CreateFooter(std::vector<Instruction>& instructions);
	};

} // namespace fasstv
