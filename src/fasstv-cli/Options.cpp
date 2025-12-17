// Created by block on 2025-12-15.

#include <fasstv-cli/Options.hpp>

#include <argparse/argparse.hpp>

// https://stackoverflow.com/a/4119881
bool ichar_equals(char a, char b) {
	return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

namespace fasstv::cli {

	OptionVariables Options::options {};

	int Options::ParseArgs(int argc, char** argv) {
		argparse::ArgumentParser program("fasstv-cli", "", argparse::default_arguments::help);

		// can't do these separately, argparse doesn't have copy/move

		argparse::ArgumentParser encode_command("encode", "", argparse::default_arguments::help);
		encode_command.add_description("Create a SSTV signal out of an image or webcam.");
		program.add_subparser(encode_command);
		{
			encode_command.add_argument("input").store_into(options.inputPath)
			  .help("Path to the input image.");
			encode_command.add_argument("--webcam")
			  .help("Specifies a webcam by (partial) device name.");
			encode_command.add_argument("-o", "--output").store_into(options.outputPath)
			  .help("Path to the output audio file.");
			encode_command.add_argument("-m", "--mode")
			  .help("Specifies SSTV mode by name or VIS code.");
			encode_command.add_argument("-r", "--samplerate").store_into(options.encode.samplerate)
			  .help("Sampling rate of the signal.");
			encode_command.add_argument("-v", "--volume").store_into(options.volume)
			  .help("Volume of the playback/output signal.");
			encode_command.add_argument("--stretch").flag().store_into(options.encode.image_stretch)
			  .help("If specified, stretches the image to fit according to the --scalemethod.");
			encode_command.add_argument("--scalemethod")
			  .help("Method to use when scaling the image. (Bilinear, bicubic, nearest, etc.) Refer to https://ffmpeg.org/ffmpeg-scaler.html for possible options.");
			encode_command.add_argument("-p", "--play").flag().store_into(options.play)
			  .help("If specified, plays audio through default speakers.");
		}

		argparse::ArgumentParser decode_command("decode", "", argparse::default_arguments::help);
		decode_command.add_description("Decode an SSTV signal from a file or microphone.");
		program.add_subparser(decode_command);
		{
			decode_command.add_argument("input").store_into(options.inputPath)
			  .help("Path to the input audio file.");
			decode_command.add_argument("-o", "--output").store_into(options.outputPath)
			  .help("Path to the output audio file.");
			decode_command.add_argument("-m", "--mode")
			  .help("Specifies SSTV mode by name or VIS code.");
			decode_command.add_argument("--microphone")
			  .help("Specifies a microphone by (partial) device name.");
		}

		argparse::ArgumentParser transcode_command("transcode", "", argparse::default_arguments::help);
		transcode_command.add_description("Run an image through an SSTV mode.");
		program.add_subparser(transcode_command);
		{
			transcode_command.add_argument("input").store_into(options.inputPath)
			  .help("Path to the input image.");
			transcode_command.add_argument("-o", "--output").store_into(options.outputPath)
			  .help("Path to the output image file.");
			transcode_command.add_argument("-m", "--mode")
			  .help("Specifies SSTV mode by name or VIS code.");
			transcode_command.add_argument("--resize-mode").flag().store_into(options.transcode.resize_mode_to_image)
			  .help("If specified, resizes the SSTV mode to the size of the input image.");
			transcode_command.add_argument("-r", "--samplerate").store_into(options.encode.samplerate)
			  .help("Sampling rate of the signal.");
			transcode_command.add_argument("-v", "--volume").store_into(options.volume)
			  .help("Volume of the playback/output signal.");
			transcode_command.add_argument("--stretch").flag().store_into(options.encode.image_stretch)
			  .help("If specified, stretches the image to fit according to the --scalemethod.");
			transcode_command.add_argument("--scalemethod")
			  .help("Method to use when scaling the image. (Bilinear, bicubic, nearest, etc.) Refer to https://ffmpeg.org/ffmpeg-scaler.html for possible options.");
			transcode_command.add_argument("-p", "--play").flag().store_into(options.play)
			  .help("If specified, plays audio through default speakers.");
		}

		try {
			program.parse_args(argc, argv);
		}
		catch (const std::exception& err) {
			std::cerr << err.what() << std::endl;
			std::cerr << program;
			std::exit(1);
		}

		if (program.is_subcommand_used(encode_command)) {
			options.fasstv_mode = FASSTVMode::Encode;
		}
		else if (program.is_subcommand_used(decode_command)) {
			options.fasstv_mode = FASSTVMode::Decode;
		}
		else if (program.is_subcommand_used(transcode_command)) {
			options.fasstv_mode = FASSTVMode::Transcode;
		}

		if (options.fasstv_mode == FASSTVMode::Encode || options.fasstv_mode == FASSTVMode::Transcode) {
			argparse::ArgumentParser* cmd = options.fasstv_mode == FASSTVMode::Encode ? &encode_command : &transcode_command;

			if (cmd->is_used("--mode")) {
				std::string modeArg = cmd->get<std::string>("--mode");
				if (!modeArg.empty()) {
					if (std::isdigit(modeArg[0]))
						options.mode = SSTV::GetMode(std::atoi(modeArg.c_str()));
					else
						options.mode = SSTV::GetMode(modeArg);
				}
			}

			if (cmd->is_used("--scalemethod")) {
				std::string scaleMethodArg = cmd->get<std::string>("--scalemethod");
				if (!scaleMethodArg.empty()) {
					for (auto& sm : ScaleMethods) {
						if (std::ranges::equal(sm.name, scaleMethodArg, ichar_equals))
							options.encode.image_resize_method = sm.flags;
					}
				}
			}

			// fallback to Robot 36 if no mode set
			if (options.mode == nullptr)
				options.mode = SSTV::GetMode("Robot 36");
		}

		return EXIT_SUCCESS;
	}

	void Options::PrintArgs() {
		LogInfo("Args:\n");
		LogInfo("Input path: {}", options.inputPath.string());
		LogInfo("Output path: {}", options.outputPath.string());
		LogInfo("Specified/expected mode: {}", options.mode ? options.mode->name : "(null)");
		LogInfo("Volume: {}", options.volume);
		LogInfo("Play audio? {}\n", options.play);

		LogInfo("Encode options:");
		LogInfo("    Sample rate: {}", options.encode.samplerate);
		LogInfo("    Separate scans? {}\n", options.encode.separate_scans);
		LogInfo("    Camera name: {}", options.encode.camera);
		LogInfo("    Camera mode: {}\n", options.encode.camera_mode);
		LogInfo("    Stretch image? {}", options.encode.image_stretch);
		LogInfo("    Resize method: {}\n", options.encode.image_resize_method);

		LogInfo("Decode options:");
		LogInfo("    Camera name: {}\n", options.decode.microphone);

		LogInfo("Transcode options:");
		LogInfo("    Resize mode to image? {}", options.transcode.resize_mode_to_image);
	}

} // namespace fasstv::cli