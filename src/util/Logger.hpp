//! Logging utilities for the core
//! Using Standard C++ <format>
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <vector>

namespace fasstv {

	/// The logger.
	struct Logger {
		enum class MessageSeverity { Debug, Info, Warning, Error, Fatal };

		static constexpr std::string_view SeverityToString(MessageSeverity sev) {
			// This must match order of Logger::MessageSeverity.
			const char* MessageSeverityStringTable[] = { "Debug", "Info", "Warn", "Error", "Fatal" };
			return MessageSeverityStringTable[static_cast<std::size_t>(sev)];
		}

		struct MessageDataUnformatted {
			std::chrono::system_clock::time_point time;
			MessageSeverity severity;
			std::string_view message;
		};

		/// Message data. This is only used by logger sinks.
		struct MessageData {
			std::chrono::system_clock::time_point time;
			MessageSeverity severity;
			std::string_view format;
			std::format_args args;
		};

		/// A sink.
		struct Sink {
			/// Output a message. This is called by the logger in Logger::VOut().
			virtual void OutputMessage(const MessageData& data) = 0;
		};

		/// Get the common instance of the logger.
		/// LogInfo() etc operates on this function only.
		static Logger& The();

		Logger(const Logger&) = delete;
		Logger(Logger&&) = delete;

		/// Attach a sink to the logger.
		///
		/// Attaching a sink will allow it to output log messages.
		void AttachSink(Sink& sink);

		/// Get the current log level.
		MessageSeverity GetLogLevel() const { return logLevel; }

		/// Set the current log level.
		void SetLogLevel(MessageSeverity newLogLevel) { logLevel = newLogLevel; }

		template <class... Args>
		inline void Debug(std::string_view fmt, Args... args) {
			VOut(MessageSeverity::Debug, fmt, std::make_format_args(args...));
		}

		template <class... Args>
		inline void Info(std::string_view fmt, Args... args) {
			VOut(MessageSeverity::Info, fmt, std::make_format_args(args...));
		}

		template <class... Args>
		inline void Warning(std::string_view fmt, Args... args) {
			VOut(MessageSeverity::Warning, fmt, std::make_format_args(args...));
		}

		template <class... Args>
		inline void Error(std::string_view fmt, Args... args) {
			VOut(MessageSeverity::Error, fmt, std::make_format_args(args...));
		}

		template <class... Args>
		inline void Fatal(std::string_view fmt, Args... args) {
			VOut(MessageSeverity::Fatal, fmt, std::make_format_args(args...));
		}

	   private:
		Logger() = default;
		void VOut(MessageSeverity severity, std::string_view format, std::format_args args);


		MessageSeverity logLevel {
//#if _DEBUG
			MessageSeverity::Debug
//#else
//			MessageSeverity::Info
//#endif
		};
		std::vector<Sink*> sinks;
	};

	template <class... Args>
	constexpr void LogDebug(std::string_view format, Args... args) {
		Logger::The().Debug(format, std::forward<Args>(args)...);
	}

	template <class... Args>
	constexpr void LogInfo(std::string_view format, Args... args) {
		Logger::The().Info(format, std::forward<Args>(args)...);
	}

	template <class... Args>
	constexpr void LogWarning(std::string_view format, Args... args) {
		Logger::The().Warning(format, std::forward<Args>(args)...);
	}

	template <class... Args>
	constexpr void LogError(std::string_view format, Args... args) {
		Logger::The().Error(format, std::forward<Args>(args)...);
	}

	template <class... Args>
	constexpr void LogFatal(std::string_view format, Args... args) {
		Logger::The().Fatal(format, std::forward<Args>(args)...);
	}

} // namespace fasstv
