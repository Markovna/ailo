#pragma once

#include <glm/glm.hpp>

namespace lightc {

enum CubeFace {
    FACE_POS_X = 0,
    FACE_NEG_X = 1,
    FACE_POS_Y = 2,
    FACE_NEG_Y = 3,
    FACE_POS_Z = 4,
    FACE_NEG_Z = 5
};

// Direction through (u, v) of a cubemap face, OpenGL convention.
inline glm::vec3 cubemapToDirection(int face, float u, float v) {
    float uc = 2.0f * u - 1.0f;
    float vc = 2.0f * v - 1.0f;

    glm::vec3 dir;
    switch (face) {
        case FACE_POS_X: dir = glm::vec3( 1.0f,   -vc,   -uc); break;
        case FACE_NEG_X: dir = glm::vec3(-1.0f,   -vc,    uc); break;
        case FACE_POS_Y: dir = glm::vec3(   uc,  1.0f,    vc); break;
        case FACE_NEG_Y: dir = glm::vec3(   uc, -1.0f,   -vc); break;
        case FACE_POS_Z: dir = glm::vec3(   uc,   -vc,  1.0f); break;
        case FACE_NEG_Z: dir = glm::vec3(  -uc,   -vc, -1.0f); break;
        default: dir = glm::vec3(0.0f, 0.0f, 1.0f); break;
    }
    return glm::normalize(dir);
}

}
