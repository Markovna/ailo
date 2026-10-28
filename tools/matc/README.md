# matc

Compiles a material definition (`.mat`) into a material package, which keeps the definition's name (`.mat`), and a
material instance (`.matinst`, see [Material instances](#material-instances)) into a material instance package.
A material provides named parameters, raster state, required vertex inputs, a shading model and
shader *functions*. The surface templates in `shaders/surface/` own `main()`, attributes, varyings, skinning and
lighting; matc joins them with the material's code for every variant.

```
matc [-I <dir>]... -o <output.mat> [--depfile <file>] [--emit-glsl <dir>] <input.mat>
matc -o <output.matinst> [--depfile <file>] [--root <assets folder>] <input.matinst>
matc --dump <package.mat|package.matinst>
```

- `-I` adds an include directory. The repository's `shaders/` folder, which holds the templates, is always searched,
  after these.
- `--depfile` writes the `.mat` file and every included template, so editing a template rebuilds the materials.
- `--emit-glsl` writes the generated GLSL of each variant, for debugging.
- `--root` is the assets folder. `.matinst` files need it to turn their paths into asset keys; `.mat` files ignore it.
- `--dump` prints a package: properties, parameter layout and defaults, variants; or an instance's values.

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
  declaration order, and is read as `materialParams_<name>`. `default` names the texture to bind when none is set,
  stored in the package: `white` (sampler2d default), `black` (cubemap default), `normal`.

### Shader code

- `fragment` (required) defines `void material(inout MaterialInputs material)` and must call
  `prepareMaterial(material)`. `material.normal` (tangent space, needs `tangents`) only has an effect
  when written before that call. Inputs: `baseColor`, and for `lit` also `roughness`, `metallic`,
  `reflectance`, `ambientOcclusion`, `emissive` (linear color added after lighting), `normal`. Defaults are in `initMaterial()` in
  `shaders/surface/surface_fragment.fs`.
- `vertex` (optional) defines `void materialVertex(inout MaterialVertexInputs material)`. It can change
  `worldPosition`, `worldNormal`, `color`/`uv0` (if required), and must write the custom `variables`,
  which the fragment code reads as `variable_<name>`.
- Writing `material.clipPosition` in the vertex block replaces the output position (normally
  `getClipFromWorldMatrix() * worldPosition`, which is also its initial value). The depth variant then runs
  `materialVertex()` too, so shadows follow the moved vertices. A sky uses it to sit on the far plane
  (`clipPosition.z = clipPosition.w`).
- Getters: `getUV0()`, `getColor()`, `getWorldPosition()`, `getWorldGeometricNormal()` (fragment),
  `getPosition()`, `getWorldFromModelMatrix()`, `getClipFromWorldMatrix()`, `getClipFromViewMatrix()`,
  `getViewFromWorldMatrix()`, `getWorldFromViewMatrix()` (vertex). The per-view and per-object uniforms in
  `shaders/common_uniforms.glsl` are available too.
- `unpackNormalMap(texel)` (fragment) turns a normal map texel into a tangent-space normal for `material.normal`.
  texc stores normal maps as x and y only (`tools/texc/README.md`), so sample them through it, not as `rgb * 2 - 1`.

Errors in the material's code are reported against the `.mat` file and line.

### Custom surface shading

With `"customSurfaceShading": true`, the fragment block also defines

```glsl
vec3 surfaceShading(const MaterialInputs materialInputs, const ShadingData shadingData, const LightData lightData)
```

which replaces the lit BRDF for each light (the directional light and every point/spot light) and returns
that light's contribution. It is called even when the fragment faces away from the light or is in its shadow
(`NdotL` or `visibility` is 0), so it can add its own ambient term. Image-based lighting is still applied
on top.

- `ShadingData`: `diffuseColor`, `f0`, `perceptualRoughness`, `roughness`
- `LightData`: `colorIntensity` (rgb, intensity in w), `l` (towards the light), `NdotL`, `worldPosition`,
  `attenuation`, `visibility` (shadowing)

## Variants

matc compiles every variant that `variantFilter` doesn't exclude; the material's code never picks one:

| Bit | Meaning | Stages |
|---|---|---|
| `SKINNING` | GPU skinning | vertex |
| `SHADOWS` | receives shadows (`lit` only) | fragment |
| `DEPTH_ONLY` | depth only (no color output); doesn't run `material()`, nor `materialVertex()` unless it writes `clipPosition` | both |

Identical SPIR-V is stored once in the package.

## Material instances

A `.matinst` file is JSON (with `//` and `/* */` comments) that sets parameters of a material:

```
{
    "material": "/materials/pbr.mat",
    "parameters": {
        "baseColorFactor": [1.0, 0.8, 0.8, 1.0],
        "normalGreenSign": -1.0
    },
    "textures": {
        "baseColorMap": "rock_BaseColor.png.tex"
    }
}
```

| Property | Meaning |
|---|---|
| `material` | required; the `.mat` source the instance uses |
| `parameters` | uniform parameter name -> value, written like a `default` (a value or a flat array, matrices column-major). Unlisted parameters keep the material's default. |
| `textures` | sampler parameter name -> texture asset. Unlisted samplers keep the material's default texture. |

Paths starting with `/` are relative to the assets folder (`--root`), keys with a source (`engine:/materials/lit.mat`,
`builtin://textures/white`) are used as is, and other paths are relative to the `.matinst` file. matc resolves them
at compile time and stores full asset keys, relative to the assets folder (`materials/pbr.mat`), which is where the
engine's file reader is rooted. A path outside the assets folder is an error. For a `.matinst` embedded under another
source, reference its material and textures with that source's keys (`engine:/...`).

matc reads the material's source (`engine:/` materials from the repository's `assets/`) to check every name and
value against its parameters, so a value can't have the wrong type. Materials of other sources can't be read and are
errors. Texture files are checked to exist, except for keys with a source. The depfile lists the `.matinst` and the
material's source, so editing the material rechecks its instances; the compiled material isn't needed.

At runtime `MaterialInstanceLoader` (registered by `RenderPlugin`) loads `.matinst` keys as a `MaterialInstance`,
loading its material and textures by their stored keys. The instance is shared by everyone who loads that key; for one of
your own, create an instance of its material and `copyParametersFrom()` it.
