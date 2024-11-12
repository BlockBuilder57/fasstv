// Created by block on 11/12/24.

#pragma once

namespace fasstv {
	struct Rect {
		int x, y;
		int w, h;

		static Rect CreateLetterbox(int box_width, int box_height, Rect rect);
	};
} // namespace fasstv
