// Created by block on 2024-11-14.

#include "ImageUtilities.hpp"

#include <util/Logger.hpp>
#include <SDL3_image/SDL_image.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

namespace fasstv {

	SDL_Surface* SampleSurface = {};
	std::uint8_t colorHolder[4] = {};
	std::uint32_t defaultColor = 0x88888888;
	std::uint32_t lastSample = -1;

	std::uint8_t* GetSampleFromSurface(int sample_x, int sample_y) {
		if (SampleSurface == nullptr)
			return reinterpret_cast<std::uint8_t*>(&defaultColor);

		std::uint32_t curSample = sample_x | (sample_y << 16);

		if (curSample != lastSample) {
			SDL_ReadSurfacePixel(SampleSurface, sample_x, sample_y, &colorHolder[0], &colorHolder[1], &colorHolder[2], &colorHolder[3]);
			//LogDebug("Sample ({}, {}) read #{:02x}{:02x}{:02x}{:02x}", sample_x, sample_y, colorHolder[0], colorHolder[1], colorHolder[2], colorHolder[3]);
			lastSample = curSample;
		}

		return &colorHolder[0];
	}

	SDL_Surface* LoadImage(std::filesystem::path inputPath) {
		if(inputPath.empty()) {
			LogError("Need a file to load!");
			return nullptr;
		}

		SDL_Surface* surfOrig = IMG_Load(inputPath.c_str());
		if(!surfOrig) {
			LogError("SDL3_image failed to load image! {}", inputPath.c_str());
			return nullptr;
		}

		return surfOrig;
	}

	SDL_Surface* RescaleImage(SDL_Surface* surf, int width, int height, int flags /*= SWS_BICUBIC*/) {
		// convert orig to RGBA32
		SDL_Surface* surfConv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);

		SDL_Surface* surfOut = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);

		SwsContext* sws_ctx = sws_getContext(
			surfConv->w, surfConv->h, AV_PIX_FMT_RGBA,
			surfOut->w, surfOut->h, AV_PIX_FMT_RGBA,
			flags, NULL, NULL, NULL
		);

		int src_linesize[4], dst_linesize[4];

		// we don't need to make new buffers, let's just get linesizes
		av_image_fill_linesizes(&src_linesize[0], AV_PIX_FMT_RGBA, surfConv->w);
		av_image_fill_linesizes(&dst_linesize[0], AV_PIX_FMT_RGBA, surfOut->w);

		// do the scale? wow this works hahaha
		sws_scale(sws_ctx, reinterpret_cast<const uint8_t* const*>(&(surfConv->pixels)),
				  src_linesize, 0, surf->h, reinterpret_cast<uint8_t* const*>(&(surfOut->pixels)), dst_linesize);

		sws_freeContext(sws_ctx);

		SDL_free(surfConv);

		SampleSurface = surfOut;
		return surfOut;
	}

} // namespace fasstv