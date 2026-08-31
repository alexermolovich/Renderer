#version 450

#include "config.h"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uint inTextureIndex;

layout(set = 1, binding = 0) uniform UBO
{
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 cameraPos;
} ubo;

layout(set = 1, binding = 1) uniform ShadowGenerationBlock
{
    ShadowGenerationUniform shadow;
} shadowGen;

layout(push_constant) uniform ShadowDepthPushConstants
{
    uint cascadeIndex;
} pc;

void main()
{
    uint cascadeIndex = min(pc.cascadeIndex, uint(MAX_SHADOW_CASCADES - 1));
    gl_Position = shadowGen.shadow.cascades[cascadeIndex].viewProj * ubo.model * vec4(inPos, 1.0);
}
