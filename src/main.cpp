// Created by block on 11/12/24.

#include <util/Logger.hpp>
#include <util/StdoutSink.hpp>
#include <SSTV.hpp>

#include <fstream>

extern "C" {
	#include <libavcodec/avcodec.h>

	#include <libavutil/channel_layout.h>
	#include <libavutil/common.h>
	#include <libavutil/frame.h>
	#include <libavutil/samplefmt.h>
}

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

int main(int argc, char** argv) {
	static_cast<void>(argc);
	static_cast<void>(argv);

	fasstv::LoggerAttachStdout();

	auto& log = fasstv::Logger::The();
	log.Debug("Built {} {}", __DATE__, __TIME__);

	fasstv::SSTV& sstv = fasstv::SSTV::The();

	log.Debug("Finding encoder for thing");
	const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
	if (!codec) {
		log.Error("Failed to get codec");
		return 1;
	}

	log.Debug("Allocing a context for codec");
	AVCodecContext* ctx = avcodec_alloc_context3(codec);
	if (!ctx) {
		log.Error("Couldn't allocate codec context");
		return 1;
	}

	log.Debug("Setting context parameters");
	ctx->sample_rate = 44100;
	ctx->bit_rate = 320000;
	ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

	log.Debug("Selecting channel layout");
	int ret = select_channel_layout(codec, &ctx->ch_layout);
	if (ret < 0) {
		log.Error("Failed to select channel layout");
		return 1;
	}

	log.Debug("Opening codec context");
	if (avcodec_open2(ctx, codec, nullptr) < 0) {
		log.Error("Couldn't open codec context");
		return 1;
	}

	log.Debug("Allocating frame");

	// packet for holding encoded output
	AVPacket* pkt = av_packet_alloc();
	if (pkt == nullptr) {
		fasstv::Logger::The().Error("Could not allocate packet");
		return 1;
	}

	// frame for holding input
	AVFrame* frame = av_frame_alloc();
	if (frame == nullptr) {
		log.Error("Could not allocate frame");
		return 1;
	}

	frame->nb_samples = ctx->frame_size;
	frame->format = ctx->sample_fmt;

	log.Debug("Copying context channel layout to frame channel layout");
	ret = av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
	if (ret < 0) {
		log.Error("Could not copy channel layout");
		return 1;
	}

	log.Debug("Get buffer for frame");
	ret = av_frame_get_buffer(frame, 0);
	if (ret < 0) {
		log.Error("Could not allocate audio data buffers");
		return 1;
	}

	log.Debug("Making some samples");

	std::vector<float> sstvsamples = {};
	//sstvsamples.push_back(0.5);
	sstvsamples = sstv.DoTheThing({0, 0, 320, 240});

	uint32_t frames_needed = sstvsamples.size() / ctx->frame_size;

	log.Debug("Creating file");
	// write to file
	std::ofstream file(std::format("{:%Y-%m-%d %H-%M-%S}.bin", floor<std::chrono::seconds>(std::chrono::system_clock::now())), std::ios::binary);

	uint32_t t = 0;
	//float tincr = 2 * M_PI * 440.0 / ctx->sample_rate;
	for (int i = 0; i < frames_needed; i++) {
		/* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
		//log.Debug("  Make frame {} writable (was writable? {})", i, !!av_frame_is_writable(frame));
		ret = av_frame_make_writable(frame);
		if (ret < 0) {
			log.Error("Failed to make frame writable!");
			return 1;
		}

		//log.Debug("  Get a pointer to sample data");
		auto* samples = (float*)frame->data[0];

		for (int j = 0; j < ctx->frame_size; j++) {
			samples[j] = sstvsamples[t];
			t++;
		}

		//log.Debug("  Encode packet");
		encode(ctx, frame, pkt, file);
	}

	log.Debug("Last packet");
	encode(ctx, nullptr, pkt, file);

	file.close();

	log.Debug("Freeing pointers");
	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&ctx);

	return 0;
}