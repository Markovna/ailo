// Expects VARYING to be defined as in (fragment) or out (vertex).
// VARIABLE_CUSTOMi / VARIABLE_CUSTOM_ATi as in surface/surface_varyings.glsl.

layout(location = 0) VARYING vec2 vertex_uv;

#if defined(VARIABLE_CUSTOM0)
layout(location = 1) VARYING vec4 VARIABLE_CUSTOM_AT0;
#endif
#if defined(VARIABLE_CUSTOM1)
layout(location = 2) VARYING vec4 VARIABLE_CUSTOM_AT1;
#endif
#if defined(VARIABLE_CUSTOM2)
layout(location = 3) VARYING vec4 VARIABLE_CUSTOM_AT2;
#endif
#if defined(VARIABLE_CUSTOM3)
layout(location = 4) VARYING vec4 VARIABLE_CUSTOM_AT3;
#endif
