#pragma once
#include <format>

namespace ailo {

namespace log {

class LogStream;

struct Loggers {
    LogStream& debug;
    LogStream& error;
    LogStream& warn;
    LogStream& info;
    LogStream& verbose;
};

extern Loggers const slog;

void write(LogStream& out, std::string_view fmt, std::format_args args);

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
   write(slog.info, fmt.get(), std::make_format_args(args...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(slog.warn, fmt.get(), std::make_format_args(args...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(slog.error, fmt.get(), std::make_format_args(args...));
}

}

#ifdef NDEBUG
// The dead branch keeps the format string and arguments checked (and used) without evaluating them.
#define LOG_INFO(...) (false ? ::ailo::log::info(__VA_ARGS__) : void())
#define LOG_WARNING(...) (false ? ::ailo::log::warn(__VA_ARGS__) : void())
#define LOG_ERROR(...) (false ? ::ailo::log::error(__VA_ARGS__) : void())
#else
#define LOG_INFO(...) ::ailo::log::info(__VA_ARGS__)
#define LOG_WARNING(...) ::ailo::log::warn(__VA_ARGS__)
#define LOG_ERROR(...) ::ailo::log::error(__VA_ARGS__)
#endif

}
