#pragma once

#include <cstdint>

//            +------------+---------+----------+
//  Variant   | DEPTH_ONLY | SHADOWS | SKINNING |
//            +------------+---------+----------+
//  Vertex          X           0         X       SHADOWS doesn't change the vertex shader
//  Fragment        X           X         0       SKINNING doesn't change the fragment shader
//  Depth           1           0         X       shadow receiving makes no sense when only writing depth

namespace ailo::material {

struct Variant {
    using type_t = uint8_t;

    static constexpr type_t SKINNING   = 0x01;
    static constexpr type_t SHADOWS    = 0x02;
    static constexpr type_t DEPTH_ONLY = 0x04;

    static constexpr type_t VERTEX_MASK   = DEPTH_ONLY | SKINNING;
    static constexpr type_t FRAGMENT_MASK = DEPTH_ONLY | SHADOWS;
    static constexpr uint32_t COUNT = 8;

    type_t key = 0;

    constexpr Variant() = default;
    constexpr explicit Variant(type_t key) : key(key) {}

    constexpr bool hasSkinning() const { return key & SKINNING; }
    constexpr bool isShadowReceiver() const { return key & SHADOWS; }
    constexpr bool isDepth() const { return key & DEPTH_ONLY; }

    constexpr bool operator==(const Variant&) const = default;

    static constexpr bool isValid(Variant v) { return !(v.isDepth() && v.isShadowReceiver()); }

    static constexpr Variant depth(Variant v) { return Variant { type_t(DEPTH_ONLY | (v.key & SKINNING)) }; }

    static constexpr Variant filterVertex(Variant v) { return Variant { type_t(v.key & VERTEX_MASK) }; }
    static constexpr Variant filterFragment(Variant v) { return Variant { type_t(v.key & FRAGMENT_MASK) }; }
};

}
