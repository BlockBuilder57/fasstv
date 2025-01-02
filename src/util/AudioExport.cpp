// Created by block on 2024-11-14.

#include <concepts>
#include <filesystem>
#include <fstream>
#include <util/AudioExport.hpp>
#include <util/Logger.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
}

namespace fasstv {

	void AVEncode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt, std::ofstream& file) {
		int ret = 0;

		// LogDebug("ctx: {}, frame: {}, pkt: {}", reinterpret_cast<void*>(ctx), reinterpret_cast<void*>(frame), reinterpret_cast<void*>(pkt));

		// send frame for encoding
		ret = avcodec_send_frame(ctx, frame);
		if(ret < 0) {
			LogError("Error sending the frame to the encoder");
			return;
		}

		// read all available output packets
		while(ret >= 0) {
			// LogDebug("We have {} packets to receive?", ret);
			ret = avcodec_receive_packet(ctx, pkt);
			if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				if(ret == AVERROR_EOF)
					LogDebug("Reached end of file");
				return;
			} else if(ret < 0) {
				LogError("Error encoding audio frame");
			}

			file.write(reinterpret_cast<const char*>(pkt->data), pkt->size);
			av_packet_unref(pkt);
		}
	}

	bool SamplesToAVCodec(std::vector<float>& samples, int samplerate, std::ofstream& file, AVCodecID format /*= AV_CODEC_ID_MP3*/, int bit_rate /*= 320000*/) {
		int ret = 0;

		LogDebug("Finding encoder");
		const AVCodec* codec = avcodec_find_encoder(format);
		if(!codec) {
			LogError("Failed to get codec");
			return false;
		}

		LogDebug("Allocing a context for codec");
		AVCodecContext* ctx = avcodec_alloc_context3(codec);
		if(!ctx) {
			LogError("Couldn't allocate codec context");
			return false;
		}

		LogDebug("Setting context parameters");
		ctx->sample_rate = samplerate;
		ctx->bit_rate = bit_rate;
		ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

		LogDebug("Selecting channel layout");
		AVChannelLayout chy = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
		ret = av_channel_layout_copy(&ctx->ch_layout, &chy);
		if(ret < 0) {
			LogError("Failed to select channel layout");
			return false;
		}

		LogDebug("Opening codec context");
		if(avcodec_open2(ctx, codec, nullptr) < 0) {
			LogError("Couldn't open codec context");
			return false;
		}

		LogDebug("Allocating frame");

		// packet for holding encoded output
		AVPacket* pkt = av_packet_alloc();
		if(pkt == nullptr) {
			fasstv::Logger::The().Error("Could not allocate packet");
			return false;
		}

		// frame for holding input
		AVFrame* frame = av_frame_alloc();
		if(frame == nullptr) {
			LogError("Could not allocate frame");
			return false;
		}

		frame->nb_samples = ctx->frame_size;
		frame->format = ctx->sample_fmt;

		LogDebug("Copying context channel layout to frame channel layout");
		ret = av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
		if(ret < 0) {
			LogError("Could not copy channel layout");
			return false;
		}

		LogDebug("Get buffer for frame");
		ret = av_frame_get_buffer(frame, 0);
		if(ret < 0) {
			LogError("Could not allocate audio data buffers");
			return false;
		}

		LogDebug("Making some samples");
		int frames_needed = samples.size() / ctx->frame_size;

		std::uint32_t t = 0;
		for(int i = 0; i < frames_needed; i++) {
			// LogDebug("  Make frame {} writable (was writable? {})", i, !!av_frame_is_writable(frame));
			ret = av_frame_make_writable(frame);
			if(ret < 0) {
				LogError("Failed to make frame writable!");
				break;
			}

			// LogDebug("  Get a pointer to sample data");
			auto* frameSamples = (float*)frame->data[0];

			for(int j = 0; j < ctx->frame_size; j++) {
				frameSamples[j] = samples[t];
				t++;
			}

			// LogDebug("  Encode packet");
			AVEncode(ctx, frame, pkt, file);
		}

		LogDebug("Last packet");
		AVEncode(ctx, nullptr, pkt, file);

		LogDebug("Freeing pointers");
		av_frame_free(&frame);
		av_packet_free(&pkt);
		avcodec_free_context(&ctx);

		return true;
	}

} // namespace fasstv