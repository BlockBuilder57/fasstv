// Created by block on 2024-11-14.

#pragma once

#include <SDL3/SDL_surface.h>

#include <cstdint>
#include <filesystem>

extern "C" {
#include <libswscale/swscale.h>
}

namespace fasstv {

	std::uint8_t* GetSampleFromSurface(int sample_x, int sample_y);
	SDL_Surface* LoadImage(std::filesystem::path inputPath);
	SDL_Surface* RescaleImage(SDL_Surface* surface, int width, int height, int flags = SWS_BICUBIC);

} // namespace fasstv
