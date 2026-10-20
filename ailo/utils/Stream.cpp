#include "Stream.h"

#include <iterator>

namespace ailo {

OutputStream& OutputStream::emit(std::string_view fmt, std::format_args args) {
    std::lock_guard const lock(m_lock);
    auto out = std::back_inserter(m_buffer);
    std::vformat_to(out, fmt, args);
    m_buffer.append("\n");
    flush();
    return *this;
}

}
