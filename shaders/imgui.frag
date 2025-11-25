#version 450 core
layout(location = 0) out vec4 fColor;

layout(binding = 1) uniform sampler2D sTexture;

layout(location = 0) in struct {
    vec4 Color;
    vec2 UV;
} In;

void main()
{
    vec4 tex = texture(sTexture, In.UV.st);
    tex = vec4(1, 1, 1, tex.r);
    fColor = In.Color * tex;
}
