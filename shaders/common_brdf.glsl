#define MEDIUMP_FLT_MAX    65504.0
#define saturateMediump(x) min(x, MEDIUMP_FLT_MAX)

float D_GGX(float roughness, float NoH, const vec3 n, const vec3 h) {
    vec3 NxH = cross(n, h);
    float a = NoH * roughness;
    float k = roughness / (dot(NxH, NxH) + a * a);
    float d = k * k * (1.0 / PI);
    return saturateMediump(d);
}

// from learn-opengl
float DistributionGGX(float roughness, float NoH)
{
    float a2 = roughness * roughness;
    float NoH2 = NoH*NoH;

    float denom = (NoH2 * (a2 - 1.0) + 1.0);
    denom = denom * denom;

    return (a2 / denom) * (1.0 / PI);
}

float distribution(float roughness, float NoH, const vec3 n, const vec3 h) {
    return DistributionGGX(roughness, NoH);
    return D_GGX(roughness, NoH, n, h);
}

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL) {
    float a2 = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

float V_SmithGGXCorrelatedFast(float roughness, float NoV, float NoL) {
    float a = roughness;
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

float Fd_Lambert() {
    return 1.0 / PI;
}

vec3 F_Schlick(const vec3 f0, float f90, float VoH) {
     // Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
     return f0 + (f90 - f0) * pow5(1.0 - VoH);
 }

 vec3 F_Schlick(const vec3 f0, float VoH) {
     float f = pow5(1.0 - VoH);
     return f + f0 * (1.0 - f);
 }

float F_Schlick(float f0, float f90, float VoH) {
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

// Disney BRDF
float Fd_Burley(float roughness, float NoV, float NoL, float LoH) {
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = F_Schlick(1.0, f90, NoL);
    float viewScatter  = F_Schlick(1.0, f90, NoV);
    return lightScatter * viewScatter * (1.0 / PI);
}

vec3 fresnel(const vec3 f0, float LoH) {
    // faster
    //return F_Schlick(f0, LoH); // f90 = 1.0

    float f90 = clamp01(dot(f0, vec3(50.0 * 0.33)));
    return F_Schlick(f0, f90, LoH);
}

float diffuse(float roughness, float NoV, float NoL, float LoH) {
    //return Fd_Lambert();
    return Fd_Burley(roughness, NoV, NoL, LoH);
}

float visibility(float roughness, float NoV, float NoL) {
    return V_SmithGGXCorrelated(roughness, NoV, NoL);
    //return V_SmithGGXCorrelatedFast(roughness, NoV, NoL);
}