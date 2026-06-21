#version 450

layout(set = 0, binding = 1) uniform texture2D ssaoRT;
layout(set = 0, binding = 3) uniform texture2D gBufferAlbedoRT;
layout(set = 0, binding = 6) uniform sampler rtSampler1;

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;


void main()
{
    vec3 albedo = texture(sampler2D(gBufferAlbedoRT, rtSampler1), inUV).rgb;
    float ao = texture(sampler2D(ssaoRT, rtSampler1), inUV).r;
    outColor = vec4(albedo * ao, 1.0);
}
