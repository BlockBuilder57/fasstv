// Created by block on 5/25/24.

#include "SSTV.hpp"

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

	void SSTV::SetMode(const std::string_view& name) {
		for (auto& mode : MODES) {
			if (mode.name != name)
				continue;

			SetMode(&mode);
			return;
		}

		SetMode(nullptr);
	}

	void SSTV::SetMode(int vis_code) {
		for (auto& mode : MODES) {
			if (mode.vis_code != vis_code)
				continue;

			SetMode(&mode);
			return;
		}

		SetMode(nullptr);
	}

	void SSTV::SetMode(Mode* mode) {
		if (mode == nullptr) {
			current_mode = nullptr;
			return;
		}

		LogInfo("Setting SSTV mode to {}", mode->name);

		current_mode = mode;
		instructions.clear();

		CreateVOXHeader();
		CreateVISHeader();

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

		// special hack for Robot monochrome modes
		// might not exist? lol
		/*if (mode->func_scan_handler == &SSTV::ScanMonochrome) {
			// i subtract the sweep count for the letterboxing to function, so let's work out what we need
			int sweeps = (mode->lines / 120) * 8;
			for (int i = 0; i < sweeps; i++) {
				instructions.push_back({ "(a) Sweep pulse", 0, 0, (InstructionFlags)(LengthUsesIndex | PitchUsesIndex) });
				instructions.push_back({ "(b) Sweep scan",  1, 0, (InstructionFlags)(LengthUsesIndex | PitchIsSweep) });
			}
		}*/

		// add the first non-looping instructions
		if (mode->instruction_loop_start > 0) {
			// I don't think there's a need to worry about extra lines here
			for (int i = 0; i < mode->instruction_loop_start; i++)
				instructions.push_back(mode->instructions_looping[i]);
		}

		for (int i = 0; i < lines; i++) {
			for (size_t j = mode->instruction_loop_start; j < mode->instructions_looping.size(); j++) {
				// found an extra line, but we don't use them - skip
				Instruction ins = mode->instructions_looping[j];
				if (!mode->uses_extra_lines && ins.flags & InstructionFlags::ExtraLine)
					continue;

				instructions.push_back(ins);
			}
		}
	}

	void SSTV::SetPixelProvider(fasstv::SSTV::PixelProviderCallback cb) {
		pixProviderFunc = cb;
	}

	SSTV::Mode* SSTV::GetMode() {
		return current_mode;
	}

	void SSTV::CreateVOXHeader() {
		instructions.push_back({"VOX Low",  100, 1900});
		instructions.push_back({"VOX Low",  100, 1500});
		instructions.push_back({"VOX Low",  100, 1900});
		instructions.push_back({"VOX Low",  100, 1500});
		instructions.push_back({"VOX High", 100, 2300});
		instructions.push_back({"VOX High", 100, 1500});
		instructions.push_back({"VOX High", 100, 2300});
		instructions.push_back({"VOX High", 100, 1500});
	}

	void SSTV::CreateVISHeader() {
		instructions.push_back({"Leader 1",   300, 1900});
		instructions.push_back({"break",      10,  1200});
		instructions.push_back({"Leader 2",   300, 1900});

		// VIS
		instructions.push_back({"VIS start",  30,  1200});
		bool parity = false;
		for (int i = 0; i < 7; i++) {
			bool bit = current_mode->vis_code & (1 << i);
			instructions.push_back({"VIS bit " + std::to_string(i), 30, bit ? 1100.f : 1300.f});

			if (bit)
				parity = !parity;
		}
		instructions.push_back({"VIS parity", 30, parity ? 1100.f : 1300.f});
		instructions.push_back({"VIS stop",   30,  1200});
	}

	SDL_Rect CreateLetterbox(int box_width, int box_height, SDL_Rect rect) {
		SDL_Rect ret {0, 0, box_width, box_height };

		// return early here if letterboxing should be disabled
		//return ret;

		// get scaling factors for dimensions
		float aspect_box = box_width / (float)box_height;
		float aspect_rect = rect.w / (float)rect.h;

		float scalar = aspect_box / aspect_rect;

		if (rect.w > rect.h) {
			// for when the width is bigger than the height (ie 16:9)
			ret.h = box_height * scalar;
			ret.y = ((box_height - ret.h) / 2);
		}
		else {
			// for when the height is bigger than the width (ie 9:16)
			// i still don't know why the math checks out here, but it does
			ret.w = box_height * (aspect_box / scalar);
			ret.x = ((box_width - ret.w) / 2);
		}

		return ret;
	}

	std::vector<float> SSTV::DoTheThing(SDL_Rect rect) {
		//if (current_mode == nullptr)
		//	return;

		current_time = 0;
		phase = 0;

		SDL_Rect letterbox = CreateLetterbox(current_mode->width, current_mode->lines, rect);

		// sampling helpers for sampling the screen
		// do it all right now to save time!
		// eventually the "realtime" idea will happen, but let's keep it simple for now

		cur_x = -1;
		cur_y = -1;

		while(!instructions.empty()) {
			Instruction* ins = &instructions[0];

			float length_ms = ins->length_ms;
			if(ins->flags & InstructionFlags::LengthUsesIndex) {
				length_ms = current_mode->timings[ins->length_ms];
				// LogDebug("Length from index: {}, {}", ins->length_ms, mode->timings[ins->length_ms]);
			}

			int len_samples = length_ms / (timestep * 1000);

			//LogDebug("New instruction \"{}\" ({} samples)", ins->name, len_samples);

			// increment a new line when we find them
			if(ins->flags & InstructionFlags::NewLine)
				cur_y++;

			/*if (ins->flags & InstructionFlags::PitchUsesIndex) {
				LogDebug("Pitch comes from index: {}, {}", ins->pitch, mode->frequencies[ins->pitch]);
			}
			else if (ins->flags & InstructionFlags::PitchIsDelegated) {
				LogDebug("Pitch would be delegated");
			}
			else {
				LogDebug("Pitch is {}", ins->pitch);
			}*/

			for(int i = 0; i < len_samples; i++) {
				float widthfrac = ((float)i / len_samples);
				cur_x = current_mode->width * widthfrac;

				// by default, just use the value
				float pitch = ins->pitch;

				if(ins->flags & InstructionFlags::PitchUsesIndex) {
					// take the pitch from an index
					pitch = current_mode->frequencies[ins->pitch];
				} else if(ins->flags & InstructionFlags::PitchIsSweep) {
					pitch = ScanSweep(current_mode, cur_x, true);
				} else if(ins->flags & InstructionFlags::PitchIsDelegated) {
					// we're about to do a new scan, delegate it

					if(current_mode->func_scan_handler != nullptr) {
						// calculate the letterbox
						bool letterbox_sides = letterbox.x > 0 && (cur_x < letterbox.x || cur_x >= letterbox.x + letterbox.w);
						bool letterbox_tops = letterbox.y > 0 && (cur_y < letterbox.y || cur_y >= letterbox.y + letterbox.h);

						std::uint8_t* pixel = nullptr;

						// calculate the sample to take when we're not drawing the letterbox
						// otherwise, the nullptr is returned and the pattern will be drawn
						if(!letterbox_sides && !letterbox_tops) {
							// where we're at along our scanline (remember that height's flipped)
							int sample_x = rect.w * (std::max(cur_x - letterbox.x, 0) / (float)letterbox.w);
							int sample_y = rect.h * (1.f - (std::max(cur_y - letterbox.y, 0) / (float)letterbox.h));

							// get pixel at that sample
							if (pixProviderFunc != nullptr)
								pixel = pixProviderFunc(sample_x, sample_y);
							else
								LogError("Pixel provider is null!!!");
						}

						pitch = current_mode->func_scan_handler(ins, cur_x, cur_y, pixel);
					} else {
						LogError("Mode {} has delegated pitch with no scan handler", current_mode->name);
						pitch = 1500.f;
					}
				}

				// we need to see how the phase will increase for the frequency we want
				// this is where the smooth pitch changes happen
				phase += Oscillator::GetPhaseInc(pitch, samplerate);
				if(phase == INFINITY)
					LogError("Fug");
				else
					while(phase > FTWO_PI * 2)
						phase -= FTWO_PI * 2;

				// add to the list of samples
				samples.push_back(osc.Value(phase));
			}

			// remove this instruction
			instructions.erase(instructions.begin());
		}

		return samples;
	}

	float SSTV::ScanSweep(Mode* mode, int pos_x, bool invert) {
		// just sweeps the range
		float factor = std::clamp((pos_x / (float)mode->width), 0.f, 1.f);
		if (invert)
			factor = 1.f - factor;

		return 1500.f + (800.f * factor);
	}

	float SSTV::ScanMonochrome(Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel) {
		if (ins == nullptr)
			return 0;

		float pitch = 1500.f;

		if(sampled_pixel == nullptr) {
			bool pattern = ((pos_x + pos_y) / 11) % 2;

			if(pattern)
				pitch += 800.f;
		} else {
			// Y = 0.30R + 0.59G + 0.11B
			pitch = (1500. + (((0.30 * sampled_pixel[0]) + (0.59 * sampled_pixel[1]) + (0.11 * sampled_pixel[2])) * 3.1372549));
		}

		return pitch;
	}

	float SSTV::ScanRGB(Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel) {
		if (ins == nullptr)
			return 0;

		float pitch = 1500.f;
		int pass = std::clamp((int)ins->pitch, 0, 2); // modes 0-2 correspond to Y/R-Y/B-Y

		if(sampled_pixel == nullptr) {
			bool pattern = ((pos_x + pos_y) / 11) % 2;

			// max to R/G to make yellow
			if(pass != 2 && pattern)
				pitch += 800.f;
		} else {
			// for bytes - (2300-1500 / 255)
			// martin is GBR
			pitch = (1500. + (sampled_pixel[pass] * 3.1372549));
		}

		return pitch;
	}

	float SSTV::ScanYRYBY(Instruction* ins, int pos_x, int pos_y, std::uint8_t* sampled_pixel) {
		float pitch = 1500.f;
		int pass = std::clamp((int)ins->pitch, 0, 2); // modes 0-2 correspond to Y/R-Y/B-Y

		std::uint8_t R = 0;
		std::uint8_t G = 0;
		std::uint8_t B = 0;

		if(sampled_pixel == nullptr) {
			bool pattern = ((pos_x + pos_y) / 11) % 2;

			if(pattern) {
				// yellow
				R = G = 255;
			}
		} else {
			R = sampled_pixel[0];
			G = sampled_pixel[1];
			B = sampled_pixel[2];
		}

		// much more complex stuff here
		// these formulas are from the ever-helpful dayton paper
		double factor = 0;

		switch(pass) {
			default:
			case 0:
				// Y
				factor = (16.0 + (0.003906 * ((65.738 * R) + (129.057 * G) + (25.064 * B))));
				break;
			case 1:
				// R-Y
				factor = (128.0 + (0.003906 * ((112.439 * R) + (-94.154 * G) + (-18.285 * B))));
				break;
			case 2:
				// B-Y
				factor = (128.0 + (0.003906 * ((-37.945 * R) + (-74.494 * G) + (112.439 * B))));
		}

		pitch = (1500. + (factor * 3.1372549));

		return pitch;
	}

} // namespace fasstv
