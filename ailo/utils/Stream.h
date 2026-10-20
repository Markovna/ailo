#pragma once
#include <format>
#include <mutex>
#include <string>

namespace ailo {

class OutputStream {
public:
    virtual ~OutputStream() = default;

    OutputStream& emit(std::string_view fmt, std::format_args args);
    std::string& getBuffer() { return m_buffer; }

protected:
    virtual OutputStream& flush() = 0;

    std::string m_buffer;
    std::recursive_mutex m_lock;
};

}
