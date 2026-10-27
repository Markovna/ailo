#include "Log.h"

#include <cstdio>
#include <iterator>
#include <string>

namespace ailo::log {

class LogStream {
public:
    enum class Level {
        VERBOSE, INFO, DEBUG, WARNING, ERROR
    };

    explicit constexpr LogStream(Level level) : m_level(level) {}

    void emit(std::string_view fmt, std::format_args args);

private:
    Level m_level;
};

static constinit LogStream cout(LogStream::Level::DEBUG);
static constinit LogStream cerr(LogStream::Level::ERROR);
static constinit LogStream cwarn(LogStream::Level::WARNING);
static constinit LogStream cinfo(LogStream::Level::INFO);
static constinit LogStream cverbose(LogStream::Level::VERBOSE);

Loggers const slog = {
    cout, cerr, cwarn, cinfo, cverbose
};

void write(LogStream& out, std::string_view fmt, std::format_args args) {
    out.emit(fmt, args);
}

void LogStream::emit(std::string_view fmt, std::format_args args) {
    thread_local std::string buffer;
    buffer.clear();
    std::vformat_to(std::back_inserter(buffer), fmt, args);
    buffer.push_back('\n');

    FILE* const file = m_level == Level::ERROR ? stderr : stdout;
    fwrite(buffer.data(), 1, buffer.size(), file);

    if (buffer.capacity() > 1024) {
        buffer.resize(1024);
        buffer.shrink_to_fit();
    }
    buffer.clear();
}

}
