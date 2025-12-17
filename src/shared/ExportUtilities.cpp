// Created by block on 2024-11-14.

#include <concepts>
#include <filesystem>
#include <fstream>
#include <shared/ExportUtilities.hpp>
#include <shared/Logger.hpp>

#define QOI_IMPLEMENTATION
#include "../../third_party/qoi/qoi.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
}

namespace fasstv {

	void stream_add_str(std::ofstream& file, std::string_view str) {
		file.write(str.begin(), str.length());
	}

	template<typename T>
	concept arithmetic = std::integral<T> or std::floating_point<T>;

	template <typename T> requires arithmetic<T>
	void stream_add_num(std::ofstream& file, T num, bool flip = false) {
		char arr[sizeof(T)] = {};
		memcpy(&arr[0], &num, sizeof(T));

		if (flip) {
			char temp;
			for (std::uint32_t i = 0; i < sizeof(T)/2; i++) {
				std::uint32_t idx = sizeof(T)-1-i;
				//LogDebug("Swapping {} ({:02x}) and {} ({:02x})?", i, arr[i], idx, arr[idx]);
				temp = arr[i];
				arr[i] = arr[idx];
				arr[idx] = temp;
			}
		}

		file.write(&arr[0], sizeof(T));
	}

	bool SamplesToWAV(std::vector<float>& samples, int samplerate, std::ofstream& file) {
		std::streampos startPos = file.tellp();
		const int channels = 1;
		const int bitDepth = sizeof(float) * 8;

		// Header chunk
		//
		stream_add_str(file, "RIFF");

		// we'll come back to this
		int fileSize = 69; // @0x4

		stream_add_num<std::uint32_t>(file, fileSize);

		// WAVE chunk
		//
		stream_add_str(file, "WAVE");

		// fmt chunk
		//
		stream_add_str(file, "fmt ");
		stream_add_num<std::uint32_t>(file, 16); // chunk size
		stream_add_num<std::uint16_t>(file, 0x0003); // format type, IEEE float
		stream_add_num<std::uint16_t>(file, channels);
		stream_add_num<std::uint32_t>(file, samplerate);

		int bytesPerSec = (channels * samplerate * bitDepth) / 8;
		stream_add_num<std::uint32_t>(file, bytesPerSec);

		std::uint16_t blockAlign = channels * (bitDepth / 8);
		stream_add_num<std::uint16_t>(file, blockAlign);

		stream_add_num<std::uint16_t>(file, bitDepth);

		// fact chunk
		//
		stream_add_str(file, "fact");
		stream_add_num<std::uint32_t>(file, 4); // chunk size
		stream_add_num<std::uint32_t>(file, samples.size());

		// DATA chunk
		//
		stream_add_str(file, "data");
		stream_add_num(file, samples.size() * sizeof(float));
		for (float& smp : samples)
			stream_add_num(file, smp);


		// seek back for filesize
		fileSize = file.tellp();
		file.seekp((int)startPos + 4);
		stream_add_num<std::uint32_t>(file, fileSize - 8);

		return true;
	}

	bool SamplesToBIN(std::vector<float>& samples, std::ofstream& file) {
		for (float& smp : samples)
			stream_add_num(file, smp);

		return true;
	}

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

	void PixelsToQOI(std::uint8_t* pixels, int width, int height, std::ofstream& file) {
		qoi_desc desc = {
			.width = width,
			.height = height,
			.channels = 3,
			.colorspace = QOI_SRGB
		};

		int size;
		void* data = qoi_encode(pixels, &desc, &size);

		file.write(static_cast<char*>(data), size);

		free(data);
	}

} // namespace fasstv