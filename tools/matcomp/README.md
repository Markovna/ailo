# matcomp

Compiles a material definition (`.mat`) into a material package (`.matpack`) that the engine loads.
A material provides named parameters, raster state, required vertex inputs, a shading model and
shader *functions*. The engine owns `main()`, attributes, varyings, skinning, lighting and the
render passes; matcomp joins the engine templates in `shaders/surface/` with the material's code
for every variant.

```
matcomp [-I <dir>]... -o <output.matpack> [--depfile <file>] [--emit-glsl <dir>] <input.mat>
matcomp --dump <package.matpack>
```

- `-I` adds an include directory (the build passes `shaders/`).
- `--depfile` writes the `.mat` file and every included template, so editing a template rebuilds the materials.
- `--emit-glsl` writes the generated GLSL of each variant, for debugging.
- `--dump` prints a package: properties, parameter layout and defaults, variants.

In CMake: `add_material(ailo materials/<name>.mat)` produces `<build>/materials/<name>.matpack`.

## Format

```
material {
    "name": "Textured",
    "shadingModel": "lit",
    "requires": ["uv0"],
    "parameters": [
        { "type": "sampler2d", "name": "baseColorMap", "default": "white" },
        { "type": "float",     "name": "roughness",    "default": 0.5 }
    ]
}

vertex {        // optional
    void materialVertex(inout MaterialVertexInputs material) { }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = texture(materialParams_baseColorMap, getUV0());
        material.roughness = materialParams.roughness;
    }
}
```

The `material` block is strict JSON (quoted keys and strings); `//` and
`/* */` comments are allowed everywhere. Unknown properties are errors.

### Material properties

| Property | Values | Default |
|---|---|---|
| `name` | string | file name |
| `shadingModel` | `lit`, `unlit` | `lit` |
| `parameters` | array, see below | none |
| `requires` | `color`, `uv0`, `tangents`, `normal` (position is always available; `lit` and `tangents` imply `normal`) | none |
| `variables` | up to 4 names of custom `vec4` interpolants | none |
| `blending` | `opaque` | `opaque` |
| `vertexDomain` | `object` | `object` |
| `culling` | `none`, `front`, `back`, `frontAndBack` | `back` |
| `colorWrite`, `depthWrite`, `depthCulling` | bool (`depthCulling: false` disables the depth test) | `true` |
| `depthFunc` | `never`, `less`, `equal`, `lessEqual`, `greater`, `notEqual`, `greaterEqual`, `always` | `less` |
| `doubleSided` | bool | `false` |
| `variantFilter` | `skinning`, `shadowReceiver`: variants the material never needs | none |
| `customSurfaceShading` | bool, `lit` only: the fragment block provides `surfaceShading()` (see below) | `false` |

### Parameters

`{ "type": <type>, "name": <identifier>, "default": <value> }`

- Uniform types: `bool`, `bool2-4`, `float`, `float2-4`, `int`, `int2-4`, `uint`, `uint2-4`, `float3x3`,
  `float4x4`, and arrays of them (`float[4]`). They live in the `MaterialParams` block (set 2, binding 0,
  std140) and are read as `materialParams.<name>`. `default` is a value or a flat array of values
  (matrices column-major); without it the value is zero.
- Sampler types: `sampler2d`, `samplerCubemap`. Each gets its own binding in set 2 starting at 1, in
  declaration order, and is read as `materialParams_<name>`. `default` names the built-in texture used
  when the material instance doesn't set one: `white` (sampler2d default), `black` (cubemap default), `normal`.

### Shader code

- `fragment` (required) defines `void material(inout MaterialInputs material)` and must call
  `prepareMaterial(material)`. `material.normal` (tangent space, needs `tangents`) only has an effect
  when written before that call. Inputs: `baseColor`, and for `lit` also `roughness`, `metallic`,
  `reflectance`, `ambientOcclusion`, `normal`. Defaults are in `initMaterial()` in
  `shaders/surface/surface_fragment.fs`.
- `vertex` (optional) defines `void materialVertex(inout MaterialVertexInputs material)`. It can change
  `worldPosition`, `worldNormal`, `color`/`uv0` (if required), and must write the custom `variables`,
  which the fragment code reads as `variable_<name>`.
- Writing `material.clipPosition` in the vertex block replaces the output position (normally
  `getClipFromWorldMatrix() * worldPosition`, which is also its initial value). The depth variant then runs
  `materialVertex()` too, so shadows follow the moved vertices. `materials/skybox.mat` uses it to put the sky
  on the far plane.
- Getters: `getUV0()`, `getColor()`, `getWorldPosition()`, `getWorldGeometricNormal()` (fragment),
  `getPosition()`, `getWorldFromModelMatrix()`, `getClipFromWorldMatrix()`, `getClipFromViewMatrix()`,
  `getViewFromWorldMatrix()`, `getWorldFromViewMatrix()` (vertex). The per-view and per-object uniforms in
  `shaders/common_uniforms.glsl` are available too.

Errors in the material's code are reported against the `.mat` file and line.

### Custom surface shading

With `"customSurfaceShading": true`, the fragment block also defines

```glsl
vec3 surfaceShading(const MaterialInputs materialInputs, const ShadingData shadingData, const LightData lightData)
```

which replaces the lit BRDF for each light (the directional light and every point/spot light) and returns
that light's contribution. It is called even when the fragment faces away from the light or is in its shadow
(`NdotL` or `visibility` is 0), so it can add its own ambient term. Image-based lighting is still applied
on top. The structures match Filament's:

- `ShadingData`: `diffuseColor`, `f0`, `perceptualRoughness`, `roughness`
- `LightData`: `colorIntensity` (rgb, intensity in w), `l` (towards the light), `NdotL`, `worldPosition`,
  `attenuation`, `visibility` (shadowing)

`materials/toon.mat` is an example, after Filament's toon shading.

## Variants

Variants are chosen by the engine per draw, never by the material (see `ailo/render/material/Variant.h`):

| Bit | Meaning | Stages |
|---|---|---|
| `SKINNING` | GPU skinning | vertex |
| `SHADOWS` | receives shadows (`lit` only) | fragment |
| `DEPTH_ONLY` | depth only, used by the shadow pass; doesn't run `material()`, nor `materialVertex()` unless it writes `clipPosition` | both |

Identical SPIR-V is stored once in the package.

## Using a material in the engine

```cpp
auto lit = assets.load<Material>(materials::kLit);                  // "materials/lit.matpack"
auto instance = MaterialInstance::create(assets, api, lit);
instance->setParameter("baseColorMap", texture);                    // samplers by name
instance->setParameter("roughnessFactor", 0.5f);                    // uniforms by name, type-checked
instance->setParameter("weights", std::span<const float>(w), 1);    // array elements from index 1
renderable.materials.push_back(instance);                           // one instance per mesh face
```

The renderer picks the variant per draw from the entity (`Skin` → `SKINNING`, `Renderable::receiveShadows` →
`SHADOWS`, the shadow pass → `DEPTH_ONLY` if `Renderable::castShadows`).
