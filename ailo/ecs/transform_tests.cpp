#include "Transform.h"

#include <cassert>
#include <cmath>
#include <iostream>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace ailo;

namespace {

constexpr float kEpsilon = 1e-4f;

bool near(float a, float b) { return std::abs(a - b) <= kEpsilon; }

bool near(const glm::vec3& a, const glm::vec3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool near(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (!near(a[c][r], b[c][r])) return false;
    return true;
}

// q and -q are the same rotation.
bool sameRotation(const glm::quat& a, const glm::quat& b) {
    return near(std::abs(glm::dot(a, b)), 1.0f);
}

bool finite(const Transform& t) {
    for (int i = 0; i < 3; i++)
        if (!std::isfinite(t.position[i]) || !std::isfinite(t.scale[i])) return false;
    for (int i = 0; i < 4; i++)
        if (!std::isfinite(t.rotation[i])) return false;
    return true;
}

Transform sample() {
    return {
        .position = { 1.0f, -2.0f, 3.5f },
        .rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, -0.5f))),
        .scale = { 2.0f, 0.5f, 3.0f },
    };
}

void testDefaultIsIdentity() {
    assert(Transform {}.toMatrix() == glm::mat4(1.0f));
}

void testTrsMatchesGlmComposition() {
    const Transform t = sample();
    const glm::mat4 expected =
        glm::translate(glm::mat4(1.0f), t.position) * glm::mat4_cast(t.rotation) * glm::scale(glm::mat4(1.0f), t.scale);
    assert(near(t.toMatrix(), expected));
}

void testTrsIsAffine() {
    assert(isAffine(sample().toMatrix()));
    assert(isAffine(sample().toMatrix() * Transform { .position = { 5, 6, 7 } }.toMatrix()));
    assert(!isAffine(glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 100.0f)));
}

void testRoundTrip() {
    const Transform t = sample();
    const Transform back = Transform::fromMatrix(t.toMatrix());
    assert(near(back.position, t.position));
    assert(near(back.scale, t.scale));
    assert(sameRotation(back.rotation, t.rotation));
}

// A mirrored basis is folded into scale.x; the matrix must still round-trip exactly.
void testNegativeScale() {
    Transform t = sample();
    t.scale = { 1.0f, -2.0f, 1.5f };
    const glm::mat4 m = t.toMatrix();
    const Transform back = Transform::fromMatrix(m);
    assert(back.scale.x < 0.0f);
    assert(near(back.toMatrix(), m));
}

// Shear can't be represented: translation survives, the rotation stays orthonormal.
void testShearIsDropped() {
    glm::mat4 m = sample().toMatrix();
    m[1] += m[0] * 0.5f;  // shear Y along X
    const Transform back = Transform::fromMatrix(m);
    assert(finite(back));
    assert(near(back.position, glm::vec3(m[3])));
    assert(near(glm::length(back.rotation), 1.0f));
    assert(isAffine(back.toMatrix()));
}

void testZeroScaleIsFinite() {
    Transform t = sample();
    t.scale = { 0.0f, 1.0f, 0.0f };
    const Transform back = Transform::fromMatrix(t.toMatrix());
    assert(finite(back));
    assert(near(back.scale, glm::vec3(0.0f, 1.0f, 0.0f)));
    assert(near(back.position, t.position));
}

// The sandbox character: glm::scale(0.01) then glm::rotate(90°, Y).
void testSandboxCharacterDecomposes() {
    glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const Transform t = Transform::fromMatrix(m);
    assert(near(t.scale, glm::vec3(0.01f)));
    assert(sameRotation(t.rotation, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))));
    assert(near(t.toMatrix(), m));
}

// Parent-child composition and the inverse used by setWorld later.
void testCompositionAndAffineInverse() {
    const glm::mat4 parent = sample().toMatrix();
    const glm::mat4 local = Transform { .position = { 0, 1, 0 }, .rotation = glm::angleAxis(1.0f, glm::vec3(0, 0, 1)) }.toMatrix();
    const glm::mat4 world = parent * local;
    assert(isAffine(world));
    assert(near(glm::affineInverse(parent) * world, local));
    assert(near(glm::affineInverse(world), glm::inverse(world)));
}

}

int main() {
    testDefaultIsIdentity();
    testTrsMatchesGlmComposition();
    testTrsIsAffine();
    testRoundTrip();
    testNegativeScale();
    testShearIsDropped();
    testZeroScaleIsFinite();
    testSandboxCharacterDecomposes();
    testCompositionAndAffineInverse();

    std::cout << "All transform tests passed" << std::endl;
    return 0;
}
