# lightc

Bakes image-based lighting data:

- a light definition (`.light`) naming an equirectangular HDR environment becomes a lighting package: its prefiltered 
  specular radiance cubemap and the spherical harmonics of its diffuse irradiance;
- `--dfg` writes the DFG LUT of the split-sum approximation as a texture package (`.tex`). It has no source.

```
lightc -o <output.light> [--depfile <file>] [--root <assets folder>] <input.light>
lightc --dfg -o <output.tex>
```

- `--root` is the assets folder that paths starting with `/` are relative to. The asset build passes it.
- `--depfile` lists the `.light` file and its source image, so editing either rebakes it.
- `texc --dump <package.tex>` prints the header of the DFG LUT.

## Outputs

**Prefiltered radiance:** an `rgba16f` cubemap of `size` with every level stored, down to 16x16 (filtered in 32-bit
float, then converted; values above 65504 are clamped). Level `i` of `n` is
filtered with the GGX lobe of perceptual roughness `r` where `lod = i / (n - 1) = r * (2 - r)`, so the level for a
roughness is found by inverting it. Level 0 is the unfiltered environment. Each GGX sample reads a mip of the
source whose texels cover its solid angle (filtered importance sampling), so few samples don't alias.

**Irradiance SH:** 9 RGB coefficients (L2). Level 0 of the prefiltered cubemap (the unfiltered environment), still in 32-bit float, is
projected onto the real SH basis, each texel weighted by its solid angle, so the SH use the same directions as the
cubemap. The coefficients are convolved with the clamped cosine (`A_l` = pi, 2pi/3, pi/4), divided
by pi and multiplied by their basis constant, so `max(sum(sh[i] * poly_i(n)), 0)` is the radiance a white Lambertian
surface with normal `n` reflects. `poly` is, in order: `1, y, z, x, y*x, y*z, 3*z*z - 1, z*x, x*x - y*y`, and the
coefficients are stored in that order.

**DFG LUT:** a 256x256 `rgba16f` 2D texture without mips. `x` is NoV, `y` is perceptual roughness, both at texel centers;
`rg` hold the multiscatter DFG terms.

## Format

Strict JSON (quoted keys and strings); `//` and `/* */` comments are allowed. Unknown properties are errors.

```
{
    "source": "environment.hdr",
    "size": 256,
    "samples": 1024
}
```

| Property  | Type   | Default | Meaning |
|-----------|--------|---------|---------|
| `source`  | string |         | Equirectangular HDR image, relative to the `.light` file, or to the assets folder if it starts with `/`. |
| `size`    | int    | `256`   | Face size of level 0, a power of two. |
| `samples` | int    | `1024`  | GGX samples per texel for the rough levels. |
| `maxRadiance` | number | none | Caps the luminance of every source texel, keeping its color, before prefiltering and the SH. Set it above the sky and below the sun to leave the sun to a directional light: otherwise the IBL lights the sun's shadows too. |
