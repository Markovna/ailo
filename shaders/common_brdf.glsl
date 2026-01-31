#define MEDIUMP_FLT_MAX    65504.0
#define saturateMediump(x) min(x, MEDIUMP_FLT_MAX)

float D_GGX(float roughness, float NoH, const vec3 h) {
    float oneMinusNoHSquared = 1.0 - NoH * NoH;
    float a = NoH * roughness;
    float k = min(roughness / (oneMinusNoHSquared + a * a), 453.5); // 453.5 prevents fp16 overflow
    float d = k * (k * (1.0 / PI));
    return d;
}

#define PREVENT_DIV0(n, d, magic)   ((n) / max(d, magic))

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL) {
    float a2 = roughness * roughness;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    float v = PREVENT_DIV0(0.5, lambdaV + lambdaL, 0.0000077);
    return v;
}

float V_SmithGGXCorrelatedFast(float roughness, float NoV, float NoL) {
    float v = PREVENT_DIV0(0.5, mix(2.0 * NoL * NoV, NoL + NoV, roughness), 0.0000077);
    return v;
}

vec3 F_Schlick(const vec3 f0, float f90, float VoH) {
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

float Fd_Lambert() {
    return 1.0 / PI;
}

vec3 fresnel(const vec3 f0, float LoH) {
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

float distribution(float roughness, float NoH, const vec3 h) {
    return D_GGX(roughness, NoH, h);
}