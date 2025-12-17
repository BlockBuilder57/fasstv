// Created by block on 2024-11-12.

#include <shared/Rect.hpp>

namespace fasstv {
	Rect Rect::CreateLetterbox(int box_width, int box_height, Rect rect) {
		Rect ret { 0, 0, box_width, box_height };

		// return early here if letterboxing should be disabled
		// return ret;

		// get scaling factors for dimensions
		float aspect_box = box_width / (float)box_height;
		float aspect_rect = rect.w / (float)rect.h;

		float scalar = aspect_box / aspect_rect;

		if(rect.w > rect.h) {
			// for when the width is bigger than the height (ie 16:9)
			ret.h = box_height * scalar;
			ret.y = ((box_height - ret.h) / 2);
		} else {
			// for when the height is bigger than the width (ie 9:16)
			// i still don't know why the math checks out here, but it does
			ret.w = box_height * (aspect_box / scalar);
			ret.x = ((box_width - ret.w) / 2);
		}

		return ret;
	}
} // namespace fasstv