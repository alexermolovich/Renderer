#version 450

#include "config.h"

layout(set = 0, binding = 1) uniform texture2D ssaoRT;
layout(set = 0, binding = 0) uniform texture2D depthRT;
layout(set = 0, binding = 3) uniform texture2D gBufferAlbedoRT;
layout(set = 0, binding = 6) uniform sampler rtSampler1;
layout(set = 0, binding = 11) uniform texture2DArray shadowCascadeDepthRT;

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform Ubo {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} ubo;

layout(set = 1, binding = 1) uniform ShadowGenerationBlock {
    ShadowGenerationUniform shadow;
} shadowGen;

// Current Lights that are being used in the scene
layout(push_constant) uniform PushConstants {
    ComposePushConstants compose;
} Lights;

vec3 reconstructWorldPosition(vec2 uv, float depth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPosition = vec4(ndc, depth, 1.0);
    vec4 worldPosition = inverse(ubo.projection * ubo.view) * clipPosition;
    return worldPosition.xyz / worldPosition.w;
}

uint selectCascade(float viewDepth)
{
    for (uint cascadeIndex = 0; cascadeIndex < uint(MAX_SHADOW_CASCADES); cascadeIndex++)
    {
        if (viewDepth <= shadowGen.shadow.cascades[cascadeIndex].splitDepth)
        {
            return cascadeIndex;
        }
    }

    return uint(MAX_SHADOW_CASCADES - 1);
}

float sampleCascadeShadow(vec3 worldPosition, uint cascadeIndex)
{
    vec4 shadowClip = shadowGen.shadow.cascades[cascadeIndex].viewProj * vec4(worldPosition, 1.0);
    if (shadowClip.w <= 0.0)
    {
        return 1.0;
    }

    vec3 shadowNdc = shadowClip.xyz / shadowClip.w;
    vec2 shadowUV = shadowNdc.xy * 0.5 + 0.5;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        shadowNdc.z < 0.0 || shadowNdc.z > 1.0)
    {
        return 1.0;
    }

    ivec3 shadowSize    = textureSize(sampler2DArray(shadowCascadeDepthRT, rtSampler1), 0);
    vec2 texelSize      = 1.0 / vec2(shadowSize.xy);
    float bias          = max(shadowGen.shadow.bias.x, 0.0005);
    float visibility    = 0.0;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 offset = vec2(x, y) * texelSize;
            float closestDepth = texture(sampler2DArray(shadowCascadeDepthRT, rtSampler1),
                                         vec3(shadowUV + offset, float(cascadeIndex))).r;
            visibility += (shadowNdc.z - bias <= closestDepth) ? 1.0 : 0.0;
        }
    }

    return visibility / 9.0;
}

void main()
{
    vec3 albedo = texture(sampler2D(gBufferAlbedoRT, rtSampler1), inUV).rgb;
    float ao = texture(sampler2D(ssaoRT, rtSampler1), inUV).r;
    float depth = texture(sampler2D(depthRT, rtSampler1), inUV).r;

    float shadowVisibility = 1.0;
    if (depth < 1.0)
    {
        vec3 worldPosition = reconstructWorldPosition(inUV, depth);
        vec3 viewPosition = (ubo.view * vec4(worldPosition, 1.0)).xyz;
        uint cascadeIndex = selectCascade(-viewPosition.z);
        shadowVisibility = sampleCascadeShadow(worldPosition, cascadeIndex);
    }

    outColor = vec4(albedo * (ao * 0.1) * shadowVisibility, 1.0);
}
