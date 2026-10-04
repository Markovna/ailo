// Expects VARYING to be defined as in (fragment) or out (vertex).
// matcomp defines, for each entry i of the material's "variables":
//   VARIABLE_CUSTOMi     the variable name (field of MaterialVertexInputs)
//   VARIABLE_CUSTOM_ATi  the varying, variable_<name>

#include "common_varyings.glsl"

// Built-in varyings use locations 0-4.
#if defined(VARIABLE_CUSTOM0)
layout(location = 5) VARYING vec4 VARIABLE_CUSTOM_AT0;
#endif
#if defined(VARIABLE_CUSTOM1)
layout(location = 6) VARYING vec4 VARIABLE_CUSTOM_AT1;
#endif
#if defined(VARIABLE_CUSTOM2)
layout(location = 7) VARYING vec4 VARIABLE_CUSTOM_AT2;
#endif
#if defined(VARIABLE_CUSTOM3)
layout(location = 8) VARYING vec4 VARIABLE_CUSTOM_AT3;
#endif
