// Created by block on 2024-11-14.

#pragma once

#include <SDL3/SDL_rect.h>

namespace fasstv {

	SDL_Rect CreateLetterbox(int box_width, int box_height, SDL_Rect rect);

} // namespace fasstv
