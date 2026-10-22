#include "Log.h"

#include <cstdio>
#include "Stream.h"

namespace ailo::log {

class LogStream : public OutputStream {
public:
    enum class Level {
        VERBOSE, INFO, DEBUG, WARNING, ERROR
    };

    constexpr explicit LogStream(Level level) : m_level(level) {}

protected:
    OutputStream& flush() override;

private:
    Level m_level;
};

static LogStream cout(LogStream::Level::DEBUG);
static LogStream cerr(LogStream::Level::ERROR);
static LogStream cwarn(LogStream::Level::WARNING);
static LogStream cinfo(LogStream::Level::INFO);
static LogStream cverbose(LogStream::Level::VERBOSE);

Loggers const slog = {
    cout, cerr, cwarn, cinfo, cverbose
};

void write(Loggers::ostream& out, std::string_view fmt, std::format_args args) {
    out.emit(fmt, args);
}

OutputStream& LogStream::flush() {
    auto& buf = getBuffer();
    switch (m_level) {
        case Level::DEBUG:
        case Level::WARNING:
        case Level::INFO:
            fwrite(buf.data(), 1, buf.size(), stdout);
            break;
        case Level::ERROR:
            fwrite(buf.data(), 1, buf.size(), stderr);
            break;
        case Level::VERBOSE:
            fwrite(buf.data(), 1, buf.size(), stdout);
            break;
    }

    if (buf.capacity() > 1024) {
        buf.resize(1024);
        buf.shrink_to_fit();
    }
    buf.clear();
    return *this;
}

}
