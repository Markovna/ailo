#pragma once
#include <format>

namespace ailo {

class OutputStream;

namespace log {

struct Loggers {
    using ostream = OutputStream;

    ostream& debug;
    ostream& error;
    ostream& warn;
    ostream& info;
    ostream& verbose;
};

extern Loggers const slog;

void write(Loggers::ostream& out, std::string_view fmt, std::format_args args);

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

#define LOG_INFO(fmt, ...) log::info(fmt, __VA_ARGS__)
#define LOG_WARNING(fmt, ...) log::warn(fmt, __VA_ARGS__)
#define LOG_ERROR(fmt, ...) log::error(fmt, __VA_ARGS__)

}
