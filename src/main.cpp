// Created by block on 11/12/24.

#include <util/Logger.hpp>
#include <util/StdoutSink.hpp>
#include <SSTV.hpp>

#include <SDL3_image/SDL_image.h>

#include <fstream>
#include <filesystem>

extern "C" {
	#include <libavcodec/avcodec.h>

	#include <libavutil/channel_layout.h>
	#include <libavutil/common.h>
	#include <libavutil/frame.h>
	#include <libavutil/samplefmt.h>
}

namespace fasstv {
	int select_channel_layout(const AVCodec *codec, AVChannelLayout *dst)
	{
		AVChannelLayout chy = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
		return av_channel_layout_copy(dst, &chy);
	}
	
	void encode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt, std::ofstream& file) {
		int ret = 0;

		//fasstv::Logger::The().Debug("ctx: {}, frame: {}, pkt: {}", reinterpret_cast<void*>(ctx), reinterpret_cast<void*>(frame), reinterpret_cast<void*>(pkt));

		// send frame for encoding
		ret = avcodec_send_frame(ctx, frame);
		if (ret < 0) {
			fasstv::Logger::The().Error("Error sending the frame to the encoder");
			return;
		}

		// read all available output packets
		while (ret >= 0) {
			//fasstv::Logger::The().Debug("We have {} packets to receive?", ret);
			ret = avcodec_receive_packet(ctx, pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				if (ret == AVERROR_EOF)
					fasstv::Logger::The().Debug("Reached end of file");
				return;
			}
			else if (ret < 0) {
				fasstv::Logger::The().Error("Error encoding audio frame");
			}

			file.write(reinterpret_cast<const char*>(pkt->data), pkt->size);
			av_packet_unref(pkt);
		}
	}

	bool samples_to_mp3(std::vector<float> sstvsamples, std::ofstream& file) {
		fasstv::Logger& log = fasstv::Logger::The();

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
		int ret = select_channel_layout(codec, &ctx->ch_layout);
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
		uint32_t frames_needed = sstvsamples.size() / ctx->frame_size;

		uint32_t t = 0;
		//float tincr = 2 * M_PI * 440.0 / ctx->sample_rate;
		for (int i = 0; i < frames_needed; i++) {
			/* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
			//LogDebug("  Make frame {} writable (was writable? {})", i, !!av_frame_is_writable(frame));
			ret = av_frame_make_writable(frame);
			if (ret < 0) {
				LogError("Failed to make frame writable!");
				return false;
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

SDL_Surface* surf = nullptr;
std::uint8_t colorHolder[4] = {};
std::uint32_t defaultColor = 0xaaaaaaa;

std::uint8_t* GetSampleFromSurface(int sample_x, int sample_y) {
	if (surf == nullptr)
		return reinterpret_cast<std::uint8_t*>(&defaultColor);

	SDL_ReadSurfacePixel(surf, sample_x, surf->h - sample_y, &colorHolder[0], &colorHolder[1], &colorHolder[2], &colorHolder[3]);
	return &colorHolder[0];
}

int main(int argc, char** argv) {
	static_cast<void>(argc);
	static_cast<void>(argv);

	fasstv::LoggerAttachStdout();

	fasstv::Logger& log = fasstv::Logger::The();
	fasstv::LogDebug("Built {} {}", __DATE__, __TIME__);

	if (argc < 2) {
		fasstv::LogError("Need a file to load!");
		return 1;
	}

	auto path = std::filesystem::path(argv[1]);
	surf = IMG_Load(path.c_str());
	if (!surf) {
		fasstv::LogError("SDL3_image failed to load texture! {}", path.c_str());
		return 1;
	}

	fasstv::SSTV& sstv = fasstv::SSTV::The();
	sstv.SetPixelProvider(&GetSampleFromSurface);
	auto samples = sstv.DoTheThing({0, 0, surf->w, surf->h});

	//std::string timestamp = std::format("{:%Y-%m-%d %H-%M-%S}", floor<std::chrono::seconds>(std::chrono::system_clock::now()));

	auto mp3path = path;
	mp3path.replace_filename(path.stem().string() + " " + sstv.GetMode()->name + ".mp3");

	std::ofstream file(mp3path.string(), std::ios::binary);
	fasstv::samples_to_mp3(samples, file);
	file.close();

	return 0;
}