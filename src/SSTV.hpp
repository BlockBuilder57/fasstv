// Created by block on 5/25/24.

#pragma once

#include <array>
#include <util/Logger.hpp>
#include <util/Oscillator.hpp>
#include <SDL3/SDL_rect.h>
#include <vector>

namespace fasstv {

	class SSTV {
	   public:
		typedef std::uint8_t* (*PixelProviderCallback)(int sample_x, int sample_y);

		static SSTV& The();

		enum InstructionFlags : std::uint8_t {
			ExtraLine        = 0b000001, // for lines that would be considered "extra"
			NewLine          = 0b000010, // indicates the start of a new line
			LengthUsesIndex  = 0b000100, // indicates that the length uses an index value in the mode
			PitchUsesIndex   = 0b001000, // indicates that the pitch uses an index value in the mode
			PitchIsDelegated = 0b010000, // indicates that the pitch is delegated to a scan handler
			PitchIsSweep     = 0b100000  // indicates that the pitch is a simple sweep (SSTV::ScanSweep)
		};

		struct Instruction {
			std::string name {};
			float length_ms {};
			float pitch {};
			InstructionFlags flags {};
		};

		struct Mode {
			std::string name;
			std::uint8_t vis_code;
			int width;
			int lines;
			bool uses_extra_lines;
			std::vector<float> timings;
			std::vector<int> frequencies;
			float (*func_scan_handler)(Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel);
			int instruction_loop_start;
			std::vector<Instruction> instructions_looping;
		};

		std::vector<Instruction> ROBOT_INSTRUCTIONS = {
			{ "(1) Sync pulse",               0, 0, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Sync porch",               1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Y scan",                   2, 0, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) \"Even\" separator pulse", 3, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Porch",                    4, 2, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(6) R-Y scan",                 5, 1, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(7) Sync pulse",               0, 0, (InstructionFlags)(ExtraLine | NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(8) Sync porch",               1, 1, (InstructionFlags)(ExtraLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(9) Y scan",                   2, 0, (InstructionFlags)(ExtraLine | LengthUsesIndex | PitchIsDelegated) },
			{ "(10) \"Odd\" separator pulse", 3, 3, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(11) Porch",                   4, 2, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(12) B-Y scan",                5, 2, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> ROBOT_MONOCHROME_INSTRUCTIONS = {
			{ "(1) Sync pulse", 0, 0, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Scan",       1, 0, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> MARTIN_INSTRUCTIONS = {
			{ "(1) Sync pulse",      0, 0, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Sync porch",      1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Green scan",      2, 1, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Separator pulse", 1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Blue scan",       2, 2, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(6) Separator pulse", 1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(7) Red scan",        2, 0, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(8) Separator pulse", 1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) }
		};

		std::vector<Instruction> WRASSE_INSTRUCTIONS = {
			{ "(1) Sync pulse", 0, 0, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Porch",      1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Green scan", 2, 0, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Blue scan",  2, 1, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(5) Red scan",   2, 2, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};


		std::vector<Instruction> SCOTTIE_INSTRUCTIONS = {
			{ "(1) \"Starting\" sync pulse ", 0, 0, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			//
			{ "(2) Separator pulse",          1, 1, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Green scan",               2, 1, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Separator pulse",          1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Blue scan",                2, 2, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(6) Sync pulse",               0, 0, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(6) Sync porch",               1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(7) Red scan",                 2, 0, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> PD_INSTRUCTIONS = {
			{ "(1) Sync pulse",              0, 0, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Porch",                   1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Y scan (from odd line)",  2, 0, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) R-Y scan",                2, 1, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(5) B-Y scan",                2, 2, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(6) Y scan (from even line)", 2, 0, (InstructionFlags)(ExtraLine | NewLine | LengthUsesIndex | PitchIsDelegated) },
		};

		std::vector<Instruction> PASOKON_INSTRUCTIONS = {
			{ "(1) Sync pulse", 0, 0, (InstructionFlags)(NewLine | LengthUsesIndex | PitchUsesIndex) },
			{ "(2) Porch",      1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(3) Red scan",   2, 0, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(4) Porch",      1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(5) Green scan", 2, 1, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(6) Porch",      1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) },
			{ "(7) Blue scan",  2, 2, (InstructionFlags)(LengthUsesIndex | PitchIsDelegated) },
			{ "(8) Porch",      1, 1, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) }
		};

		std::vector<Mode> MODES = {
			// Robot
			{ "Robot 12", 0,
			  160, 120, true,
			  {9.0f, 3.0f, 88.0f, 4.5f, 1.5f, 44.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  &SSTV::ScanYRYBY, 0, ROBOT_INSTRUCTIONS
			},
			{ "Robot 24", 4,
			  160, 120, true,
			  {9.0f, 3.0f, 138.0f, 4.5f, 1.5f, 69.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  &SSTV::ScanYRYBY, 0, ROBOT_INSTRUCTIONS
			},
			{ "Robot 36", 8,
			  320, 240, true,
			  {9.0f, 3.0f, 88.0f, 4.5f, 1.5f, 44.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  &SSTV::ScanYRYBY, 0, ROBOT_INSTRUCTIONS
			},
			{ "Robot 72", 12,
			  320, 240, false,
			  {9.0f, 3.0f, 138.0f, 4.5f, 1.5f, 69.0f}, // sync pulse, sync porch, Y scan, separator pulse, porch, R-Y/B-Y scan
			  {1200, 1500, 1900, 2300}, // sync pulse, sync porch/even separator pulse, porch, odd separator pulse
			  &SSTV::ScanYRYBY, 0, ROBOT_INSTRUCTIONS
			},
			{ "B&W 8", 2,
			  160, 120, false,
			  {10.0f, 56.0f}, // sync, scan
			  {1200}, // sync
			  &SSTV::ScanMonochrome, 0, ROBOT_MONOCHROME_INSTRUCTIONS
			},
			{ "B&W 12", 6,
			  160, 120, false,
			  {7.0f, 93.0f}, // sync, scan
			  {1200}, // sync
			  &SSTV::ScanMonochrome, 0, ROBOT_MONOCHROME_INSTRUCTIONS
			},
			{ "B&W 24", 10,
			  320, 240, false,
			  {12.0f, 93.0f}, // sync, scan
			  {1200}, // sync
			  &SSTV::ScanMonochrome, 0, ROBOT_MONOCHROME_INSTRUCTIONS
			},
			{ "B&W 36", 14,
			  320, 240, false,
			  {12.0f, 93.0f}, // sync, scan
			  {1200}, // sync
			  &SSTV::ScanMonochrome, 0, ROBOT_MONOCHROME_INSTRUCTIONS
			},

			// Martin
			{ "Martin 1", 44,
			  320, 256, false,
			  {4.862f, 0.572f, 146.432f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  &SSTV::ScanRGB, 0, MARTIN_INSTRUCTIONS
			},
			{ "Martin 2", 40,
			  320, 256, false,
			  {4.862f, 0.572f, 73.216f}, // pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanRGB, 0, MARTIN_INSTRUCTIONS
			},
			{ "Martin 3", 36,
			  128, 256, false,
			  {4.862f, 0.572f, 146.432f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  &SSTV::ScanRGB, 0, MARTIN_INSTRUCTIONS
			},
			{ "Martin 4", 32,
			  128, 256, false,
			  {4.862f, 0.572f, 73.216f}, // pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanRGB, 0, MARTIN_INSTRUCTIONS
			},

			// Wraase
			{ "Wraase SC2-180", 55,
			  320, 256, false,
			  {5.5225f, 0.500f, 235.000f}, // pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanRGB, 0, WRASSE_INSTRUCTIONS
			},

			// Scottie
			{ "Scottie 1", 60,
			  320, 256, false,
			  {9.0f, 1.5f, 138.240f}, // sync pulse, separator pulse
			  {1200, 1500}, // sync pulse, separator pulse
			  &SSTV::ScanRGB, 1, SCOTTIE_INSTRUCTIONS
			},
			{ "Scottie 2", 56,
			  320, 256, false,
			  {9.0f, 1.5f, 88.064f}, // sync pulse, separator pulse
			  {1200, 1500}, // sync pulse, separator pulse
			  &SSTV::ScanRGB, 1, SCOTTIE_INSTRUCTIONS
			},
			{ "Scottie DX", 76,
			  320, 256, false,
			  {9.0f, 1.5f, 345.6f}, // sync pulse, separator pulse
			  {1200, 1500}, // sync pulse, separator pulse
			  &SSTV::ScanRGB, 1, SCOTTIE_INSTRUCTIONS
			},

			// PD
			{ "PD50", 93,
			  320, 256, true,
			  {20.000f, 2.080f, 91.520f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanYRYBY, 0, PD_INSTRUCTIONS
			},
			{ "PD90", 99,
			  320, 256, true,
			  {20.000f, 2.080f, 170.240f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanYRYBY, 0, PD_INSTRUCTIONS
			},
			{ "PD120", 95,
			  640, 496, true,
			  {20.000f, 2.080f, 121.600f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanYRYBY, 0, PD_INSTRUCTIONS
			},
			{ "PD160", 98,
			  512, 400, true,
			  {20.000f, 2.080f, 195.584f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanYRYBY, 0, PD_INSTRUCTIONS
			},
			{ "PD180", 96,
			  640, 496, true,
			  {20.000f, 2.080f, 183.040f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanYRYBY, 0, PD_INSTRUCTIONS
			},
			{ "PD240", 97,
			  640, 496, true,
			  {20.000f, 2.080f, 244.480f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanYRYBY, 0, PD_INSTRUCTIONS
			},
			{ "PD290", 94,
			  800, 616, true,
			  {20.000f, 2.080f, 228.800f}, // sync pulse, porch, color scan
			  {1200, 1500}, // sync pulse, porch
			  &SSTV::ScanYRYBY, 0, PD_INSTRUCTIONS
			},

			// Pasokon
			{ "Pasokon P3", 113,
			  640, 496, false,
			  {5.208f, 1.042f, 133.333f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  &SSTV::ScanRGB, 0, PASOKON_INSTRUCTIONS
			},
			{ "Pasokon P5", 114,
			  640, 496, false,
			  {7.813f, 1.563f, 200.000f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  &SSTV::ScanRGB, 0, PASOKON_INSTRUCTIONS
			},
			{ "Pasokon P7", 115,
			  640, 496, false,
			  {10.417f, 1.042f, 266.666f}, // pulse, porch, color scan
			  {1200, 1500}, // pulse, porch
			  &SSTV::ScanRGB, 0, PASOKON_INSTRUCTIONS
			}
		};

		SSTV();

		void SetMode(const std::string_view& name);
		void SetMode(int vis_code);
		void SetMode(Mode* mode);
		void SetPixelProvider(PixelProviderCallback cb);

		SSTV::Mode* GetMode();

		std::vector<float> DoTheThing(SDL_Rect rect);

		static float ScanSweep(Mode* mode, int pos_x, bool invert);
		static float ScanMonochrome(Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel);
		static float ScanRGB(Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel);
		static float ScanYRYBY(Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel);

	   private:
		const int samplerate = 44100;
		const float timestep = 1.f / samplerate;

		void CreateVOXHeader();
		void CreateVISHeader();

		std::vector<Instruction> instructions {};
		std::vector<float> samples {};
		Mode* current_mode = nullptr;
		float current_time = 0;
		float phase = 0;
		int cur_x = -1;
		int cur_y = -1;
		Oscillator osc { kOsc_Sin };
		PixelProviderCallback pixProviderFunc {};
	};

} // namespace fasstv
