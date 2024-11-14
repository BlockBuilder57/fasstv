// Created by block on 11/12/24.

#include <util/Logger.hpp>
#include <util/StdoutSink.hpp>
#include <SSTV.hpp>

#include <cargs.h>
#include <SDL3_image/SDL_image.h>

#include <fstream>
#include <filesystem>

extern "C" {
	#include <libavcodec/avcodec.h>

	#include <libavutil/channel_layout.h>
	#include <libavutil/common.h>
	#include <libavutil/frame.h>
	#include <libavutil/samplefmt.h>
	#include <libavutil/imgutils.h>

	#include <libswscale/swscale.h>
}

namespace fasstv {
	void encode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt, std::ofstream& file) {
		int ret = 0;

		//LogDebug("ctx: {}, frame: {}, pkt: {}", reinterpret_cast<void*>(ctx), reinterpret_cast<void*>(frame), reinterpret_cast<void*>(pkt));

		// send frame for encoding
		ret = avcodec_send_frame(ctx, frame);
		if (ret < 0) {
			LogError("Error sending the frame to the encoder");
			return;
		}

		// read all available output packets
		while (ret >= 0) {
			//LogDebug("We have {} packets to receive?", ret);
			ret = avcodec_receive_packet(ctx, pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				if (ret == AVERROR_EOF)
					LogDebug("Reached end of file");
				return;
			}
			else if (ret < 0) {
				LogError("Error encoding audio frame");
			}

			file.write(reinterpret_cast<const char*>(pkt->data), pkt->size);
			av_packet_unref(pkt);
		}
	}

	bool samples_to_mp3(std::vector<float> sstvsamples, std::ofstream& file) {
		int ret = 0;

		LogDebug("Finding encoder");
		const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
		if (!codec) {
			LogError("Failed to get codec");
			return false;
		}

		LogDebug("Allocing a context for codec");
		AVCodecContext* ctx = avcodec_alloc_context3(codec);
		if (!ctx) {
			LogError("Couldn't allocate codec context");
			return false;
		}

		LogDebug("Setting context parameters");
		ctx->sample_rate = 44100;
		ctx->bit_rate = 320000;
		ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

		LogDebug("Selecting channel layout");
		AVChannelLayout chy = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
		ret = av_channel_layout_copy(&ctx->ch_layout, &chy);
		if (ret < 0) {
			LogError("Failed to select channel layout");
			return false;
		}

		LogDebug("Opening codec context");
		if (avcodec_open2(ctx, codec, nullptr) < 0) {
			LogError("Couldn't open codec context");
			return false;
		}

		LogDebug("Allocating frame");

		// packet for holding encoded output
		AVPacket* pkt = av_packet_alloc();
		if (pkt == nullptr) {
			fasstv::Logger::The().Error("Could not allocate packet");
			return false;
		}

		// frame for holding input
		AVFrame* frame = av_frame_alloc();
		if (frame == nullptr) {
			LogError("Could not allocate frame");
			return false;
		}

		frame->nb_samples = ctx->frame_size;
		frame->format = ctx->sample_fmt;

		LogDebug("Copying context channel layout to frame channel layout");
		ret = av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
		if (ret < 0) {
			LogError("Could not copy channel layout");
			return false;
		}

		LogDebug("Get buffer for frame");
		ret = av_frame_get_buffer(frame, 0);
		if (ret < 0) {
			LogError("Could not allocate audio data buffers");
			return false;
		}

		LogDebug("Making some samples");
		int frames_needed = sstvsamples.size() / ctx->frame_size;

		std::uint32_t t = 0;
		for (int i = 0; i < frames_needed; i++) {
			//LogDebug("  Make frame {} writable (was writable? {})", i, !!av_frame_is_writable(frame));
			ret = av_frame_make_writable(frame);
			if (ret < 0) {
				LogError("Failed to make frame writable!");
				break;
			}

			//LogDebug("  Get a pointer to sample data");
			auto* frameSamples = (float*)frame->data[0];

			for (int j = 0; j < ctx->frame_size; j++) {
				frameSamples[j] = sstvsamples[t];
				t++;
			}

			//LogDebug("  Encode packet");
			encode(ctx, frame, pkt, file);
		}

		LogDebug("Last packet");
		encode(ctx, nullptr, pkt, file);

		LogDebug("Freeing pointers");
		av_frame_free(&frame);
		av_packet_free(&pkt);
		avcodec_free_context(&ctx);

		return true;
	}
}

static struct cag_option options[] = {
	{ .identifier = 'i', .access_letters = "i", .access_name = "input", .value_name = "<image file>", .description = "Path of input image" },
	{ .identifier = 'o', .access_letters = "o", .access_name = "output", .value_name = nullptr, .description = "Path of output audio. Defaults to the input path, with the mode appended, and with an mp3 extension." },
	{ .identifier = 'f', .access_letters = "f", .access_name = "format", .value_name = "<SSTV format|VIS code>", .description = "Specifies SSTV format by name or VIS code." },
	{ .identifier = 'h', .access_letters = nullptr, .access_name = "help", .value_name = nullptr, .description = "Show help" }
};

SDL_Surface* surfOrig = nullptr, *surfOut = nullptr;
std::uint8_t colorHolder[4] = {};
std::uint32_t defaultColor = 0x88888888;

std::uint8_t* GetSampleFromSurface(int sample_x, int sample_y) {
	if (surfOut == nullptr)
		return reinterpret_cast<std::uint8_t*>(&defaultColor);

	SDL_ReadSurfacePixel(surfOut, sample_x, surfOut->h - sample_y, &colorHolder[0], &colorHolder[1], &colorHolder[2], &colorHolder[3]);
	return &colorHolder[0];
}

int main(int argc, char** argv) {
	std::filesystem::path inputPath {};
	std::filesystem::path outputPath {};

	fasstv::LoggerAttachStdout();
	fasstv::LogDebug("Built {} {}", __DATE__, __TIME__);

	fasstv::SSTV& sstv = fasstv::SSTV::The();

	cag_option_context context;
	cag_option_init(&context, options, CAG_ARRAY_SIZE(options), argc, argv);
	while (cag_option_fetch(&context)) {
		switch (cag_option_get_identifier(&context)) {
			case 'i': {
				const char* chary = cag_option_get_value(&context);
				if (chary != nullptr)
					inputPath = std::filesystem::path(chary);
				break;
			}
			case 'o': {
				const char* chary = cag_option_get_value(&context);
				if (chary != nullptr)
					outputPath = std::filesystem::path(chary);
				break;
			}
			case 'f': {
				const char* chary = cag_option_get_value(&context);
				if (chary != nullptr) {
					if (isdigit(chary[0]))
						sstv.SetMode(std::atoi(chary));
					else
						sstv.SetMode(chary);
				}
				break;
			}
			case 'h':
				printf("Usage: fasstv [OPTIONS]...\n");
				printf("Quickly converts an image file into a valid SSTV signal.\n\n");
				cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
				return EXIT_SUCCESS;
			case '?':
				cag_option_print_error(&context, stdout);
				break;
		}
	}

	for (int param_index = cag_option_get_index(&context); param_index < argc; ++param_index) {
		//printf("additional parameter: %s\n", argv[param_index]);
	}

	if (inputPath.empty()) {
		fasstv::LogError("Need a file to load!");
		return 1;
	}

	surfOrig = IMG_Load(inputPath.c_str());
	if (!surfOrig) {
		fasstv::LogError("SDL3_image failed to load texture! {}", inputPath.c_str());
		return 1;
	}

	// fallback to Robot 36 if no mode set
	if (sstv.GetMode() == nullptr)
		sstv.SetMode("Robot 36");

	fasstv::SSTV::Mode* mode = sstv.GetMode();
	surfOut = SDL_CreateSurface(mode->width, mode->lines, SDL_PIXELFORMAT_RGBA32);

	// convert orig to RGBA32 and make sure to free (since we need to do some pointer juggling)
	SDL_Surface* surfTemp = SDL_ConvertSurface(surfOrig, SDL_PIXELFORMAT_RGBA32);
	SDL_free(surfOrig);
	surfOrig = surfTemp;

	SwsContext* sws_ctx = sws_getContext(
		surfOrig->w, surfOrig->h, AV_PIX_FMT_RGBA,
		surfOut->w, surfOut->h, AV_PIX_FMT_RGBA,
		SWS_BICUBIC, NULL, NULL, NULL
	);

	int src_linesize[4], dst_linesize[4];

	// we don't need to make new buffers, let's just get linesizes
	av_image_fill_linesizes(&src_linesize[0], AV_PIX_FMT_RGBA, surfOrig->w);
	av_image_fill_linesizes(&dst_linesize[0], AV_PIX_FMT_RGBA, surfOut->w);

	// do the scale? wow this works hahaha
	sws_scale(sws_ctx, reinterpret_cast<const uint8_t * const*>(&(surfOrig->pixels)),
			  src_linesize, 0, surfOrig->h, reinterpret_cast<uint8_t * const*>(&(surfOut->pixels)), dst_linesize);

	sstv.SetPixelProvider(&GetSampleFromSurface);
	auto samples = sstv.DoTheThing({0, 0, surfOut->w, surfOut->h});
	for (float& smp : samples)
		smp *= 0.6f;

	if (outputPath.empty()) {
		outputPath = inputPath;
		outputPath.replace_filename(inputPath.stem().string() + " " + sstv.GetMode()->name + ".mp3");
	}

	std::ofstream file(outputPath.string(), std::ios::binary);
	fasstv::samples_to_mp3(samples, file);
	file.close();

	return EXIT_SUCCESS;
}