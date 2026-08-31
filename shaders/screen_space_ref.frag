#version 450

#include "config.h"

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor; // RGB: Reflection Color, A: Hit Confidence (0 or 1)

layout(set = 1, binding = 0) uniform Ubo {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
    mat4 invProjection; // Required for depth reconstruction
} ubo;

layout(set = 0, binding = 0) uniform texture2D depthRT;
layout(set = 0, binding = 1) uniform texture2D ssaoRT;
layout(set = 0, binding = 3) uniform texture2D gBufferAlbedoRT; 
layout(set = 0, binding = 4) uniform texture2D gBufferNormalSpecularRT;

layout(set = 0, binding = 6) uniform sampler rtSampler1;

const float maxDistance = 15.0;
const int   steps       = 30;
const float thickness   = 0.5;

vec3 decodeNormalOct(vec2 encoded)
{
    vec2 f = encoded * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return normalize(n);
}

// Reconstruct view-space position from Vulkan depth buffer
vec3 getViewPos(vec2 uv) {
    float depth = texture(sampler2D(depthRT, rtSampler1), uv).r;
    
    // Vulkan NDC: XY in [-1, 1], Z in [0, 1]
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = ubo.invProjection * ndc;
    
    return viewPos.xyz / viewPos.w;
}

void main() {
    // 1. Fetch current pixel properties
    vec4 normSpec = texture(sampler2D(gBufferNormalSpecularRT, rtSampler1), inUV);
    vec3 viewNormal = decodeNormalOct(normSpec.rg);
    vec3 worldNormal = normalize(transpose(mat3(ubo.view)) * viewNormal);
    float specularity = normSpec.w;

    // Sponza's floor is upward-facing. Reject walls, columns, and ceilings.
    if (worldNormal.y < 0.85 || specularity < 0.01) {
        outColor = vec4(0.0);
        return;
    }

    vec3 viewPos = getViewPos(inUV);

    // 2. Calculate view-space reflection vector
    vec3 rayDir = normalize(reflect(viewPos, viewNormal));
    vec3 rayStep = rayDir * (maxDistance / float(steps));
    vec3 currentPos = viewPos + rayStep;

    vec4 ssrResult = vec4(0.0); // Default to no reflection hit

    // 3. Linear Ray Marching
    for (int i = 0; i < steps; ++i) {
        // Project 3D view-space coordinate to [0, 1] screen UV
        vec4 projCoord = ubo.projection * vec4(currentPos, 1.0);
        projCoord.xy /= projCoord.w;
        projCoord.xy = projCoord.xy * 0.5 + 0.5;

        // Break if ray leaves screen boundaries
        if (projCoord.x < 0.0 || projCoord.x > 1.0 || 
            projCoord.y < 0.0 || projCoord.y > 1.0) {
            break;
        }

        // Depth check against scene geometry
        vec3 sampledViewPos = getViewPos(projCoord.xy);
        float depthDiff = currentPos.z - sampledViewPos.z;

        // Intersection hit check
        if (depthDiff >= 0.0 && depthDiff < thickness) {
            // Grab isolated reflection color from Albedo G-Buffer
            vec3 hitColor = texture(sampler2D(gBufferAlbedoRT, rtSampler1), projCoord.xy).rgb;
            
            // Output RGB color and set Alpha to 1.0 (Hit) multiplied by surface specularity
            ssrResult = vec4(hitColor, specularity);
            break;
        }

        currentPos += rayStep;
    }

    // Output raw SSR pass result only
    outColor = ssrResult;
}
