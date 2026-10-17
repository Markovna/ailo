#pragma once

#include <optional>
#include <string_view>

#include "render/texture/TexturePackage.h"

namespace texc {

enum class Usage : uint8_t {
    Color,
    Normal,
    Hdr,
};

enum class Quality : uint8_t {
    Low,
    Normal,
    High,
};

// The GPU family the packages are built for; each has its own format table.
enum class Target : uint8_t {
    Desktop,
};

std::string_view toString(Usage usage);
std::string_view toString(Quality quality);
std::string_view toString(Target target);

std::optional<Usage> parseUsage(std::string_view name);
std::optional<Quality> parseQuality(std::string_view name);
std::optional<Target> parseTarget(std::string_view name);

// A "format" property ("bc7", "rgba8", ...): the format without its sRGB variant, which withSrgb() picks.
std::optional<ailo::texture::TextureFormat> parseFormat(std::string_view name);

// The sRGB variant of format, or nullopt if it has none.
std::optional<ailo::texture::TextureFormat> withSrgb(ailo::texture::TextureFormat format);

// The format the target's table picks for a texture's usage and quality, and for whether it has alpha. sRGB is not
// part of the table: the caller applies withSrgb() to color textures that are sRGB.
ailo::texture::TextureFormat chooseFormat(Target target, Usage usage, Quality quality, bool alpha);

bool supports(Target target, ailo::texture::TextureFormat format);

}
