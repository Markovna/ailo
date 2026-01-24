#define PI 3.14159265359

#define MEDIUMP_FLT_MAX             65504.0

#define clamp01(x) clamp(x, 0.0, 1.0)

void toTangentFrame(const vec4 q, out vec3 n) {
    n = vec3( 0.0,  0.0,  1.0) +
        vec3( 2.0, -2.0, -2.0) * q.x * q.zwx +
        vec3( 2.0,  2.0, -2.0) * q.y * q.wzy;
}

void toTangentFrame(const vec4 q, out vec3 n, out vec3 t) {
    toTangentFrame(q, n);
    t = vec3( 1.0,  0.0,  0.0) +
        vec3(-2.0,  2.0, -2.0) * q.y * q.yxw +
        vec3(-2.0,  2.0,  2.0) * q.z * q.zwx;
}

#define MIN_N_DOT_V 1e-4

float clampNoV(float NoV) {
    // Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
    return max(NoV, MIN_N_DOT_V);
}

float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}