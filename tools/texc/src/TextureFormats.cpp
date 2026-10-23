#include "TextureFormats.h"

#include <span>
#include <stdexcept>

using ailo::TextureFormat;

namespace texc {

namespace {

enum class Alpha : uint8_t {
    Any,
    Opaque,     // only matches textures without alpha
};

struct FormatRule {
    Usage usage;
    std::optional<Quality> quality;     // nullopt: any quality
    Alpha alpha;
    TextureFormat format;
};

// The first rule that matches wins. Opaque color drops to BC1 (half the size of BC3/BC7) unless the quality is high.
constexpr FormatRule kDesktopFormats[] = {
    { Usage::Color,  Quality::Low,    Alpha::Opaque, TextureFormat::BC1 },
    { Usage::Color,  Quality::Low,    Alpha::Any,    TextureFormat::BC3 },
    { Usage::Color,  Quality::Normal, Alpha::Opaque, TextureFormat::BC1 },
    { Usage::Color,  Quality::Normal, Alpha::Any,    TextureFormat::BC3 },
    { Usage::Color,  Quality::High,   Alpha::Any,    TextureFormat::BC7 },
    { Usage::Normal, std::nullopt,    Alpha::Any,    TextureFormat::BC5 },
    { Usage::Hdr,    std::nullopt,    Alpha::Any,    TextureFormat::BC6H },
};

std::span<const FormatRule> formatTable(Target target) {
    switch (target) {
        case Target::Desktop: return kDesktopFormats;
    }
    throw std::logic_error("unknown target");
}

}

std::string_view toString(Usage usage) {
    switch (usage) {
        case Usage::Color:  return "color";
        case Usage::Normal: return "normal";
        case Usage::Hdr:    return "hdr";
    }
    return "?";
}

std::string_view toString(Quality quality) {
    switch (quality) {
        case Quality::Low:    return "low";
        case Quality::Normal: return "normal";
        case Quality::High:   return "high";
    }
    return "?";
}

std::string_view toString(Target target) {
    switch (target) {
        case Target::Desktop: return "desktop";
    }
    return "?";
}

std::optional<Usage> parseUsage(std::string_view name) {
    for (auto usage : { Usage::Color, Usage::Normal, Usage::Hdr }) {
        if (name == toString(usage)) return usage;
    }
    return std::nullopt;
}

std::optional<Quality> parseQuality(std::string_view name) {
    for (auto quality : { Quality::Low, Quality::Normal, Quality::High }) {
        if (name == toString(quality)) return quality;
    }
    return std::nullopt;
}

std::optional<Target> parseTarget(std::string_view name) {
    if (name == toString(Target::Desktop)) return Target::Desktop;
    return std::nullopt;
}

std::optional<TextureFormat> parseFormat(std::string_view name) {
    for (auto format : { TextureFormat::RGBA8, TextureFormat::RGBA16F, TextureFormat::RGBA32F, TextureFormat::BC1,
                         TextureFormat::BC3, TextureFormat::BC4, TextureFormat::BC5, TextureFormat::BC6H,
                         TextureFormat::BC7 }) {
        if (name == ailo::toString(format)) return format;
    }
    return std::nullopt;
}

std::optional<TextureFormat> withSrgb(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8: return TextureFormat::RGBA8_SRGB;
        case TextureFormat::BC1:   return TextureFormat::BC1_SRGB;
        case TextureFormat::BC3:   return TextureFormat::BC3_SRGB;
        case TextureFormat::BC7:   return TextureFormat::BC7_SRGB;
        default:                   return ailo::isSrgb(format) ? std::optional(format) : std::nullopt;
    }
}

TextureFormat chooseFormat(Target target, Usage usage, Quality quality, bool alpha) {
    for (const auto& rule : formatTable(target)) {
        if (rule.usage != usage) continue;
        if (rule.quality && *rule.quality != quality) continue;
        if (rule.alpha == Alpha::Opaque && alpha) continue;
        return rule.format;
    }
    throw std::logic_error("the " + std::string(toString(target)) + " format table has no rule for " +
                           std::string(toString(usage)) + " textures of " + std::string(toString(quality)) + " quality");
}

bool supports(Target target, TextureFormat format) {
    switch (target) {
        case Target::Desktop: return true;
    }
    return false;
}

}
