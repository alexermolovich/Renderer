#version 450

const int   SSAO_KERNEL_SIZE = 32;
const float SSAO_RADIUS      = 0.5;

layout(set = 0, binding = 0) uniform texture2D depthRT;
layout(set = 0, binding = 1) uniform texture2D ssaoRT;
layout(set = 0, binding = 2) uniform texture2D ssaoBlurRT;
layout(set = 0, binding = 3) uniform texture2D gBufferAlbedoRT;
layout(set = 0, binding = 4) uniform texture2D gBufferNormalSpecularRT;
layout(set = 0, binding = 5) uniform texture2D meshTextures[2];

layout(set = 0, binding = 6) uniform sampler rtSampler1;
layout(set = 0, binding = 7) uniform sampler rtSampler2;
layout(set = 0, binding = 8) uniform sampler textureSampler;

layout(set = 0, binding = 9) uniform texture2D ssaoNoise;

layout(set = 0, binding = 10) uniform SsaoKernelsBlock {
    vec4 ssaoKernels[SSAO_KERNEL_SIZE];
} ssaoData;

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outOcclusion;

layout(set = 1, binding = 0) uniform Ubo {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} ubo; 


vec3 reconstructPosition(vec2 uv, float z, mat4 invMat)
{
    float x = uv.x * 2.0 - 1.0;
    float y = (1.0 - uv.y) * 2.0 - 1.0;
    vec4 positionClip = vec4(x, y, z, 1.0);
    vec4 positionWorld = invMat * positionClip;
    return positionWorld.xyz / positionWorld.w;
}

vec3 decodeNormalOct(vec2 f)
{
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    vec2 zComp = -t * vec2(sign(n.x + 0.01), sign(n.y + 0.01));
    n.xy += zComp;
    return normalize(n);
}

void main()
{
    /* sample depth as float from the depth texture (combined sampler) */
    float depth = texture(sampler2D(depthRT, rtSampler1), inUV).r;
    
    vec2   normal_xy = texture(sampler2D(gBufferNormalSpecularRT, rtSampler1), inUV).rg;
    vec3   normal    = decodeNormalOct(normal_xy);
    mat4   invProj   = inverse(ubo.projection);
    vec3   fragPos   = reconstructPosition(inUV, depth, invProj);

    ivec2  texDim    = textureSize(sampler2D(gBufferNormalSpecularRT, rtSampler1), 0);
    ivec2  noiseDim  = textureSize(sampler2D(ssaoNoise, rtSampler1), 0);
    vec2   noiseUV   = inUV * vec2(texDim) / vec2(noiseDim);

    vec3   randomVec = normalize(texture(sampler2D(ssaoNoise, rtSampler1), noiseUV).xyz * 2.0 - 1.0);
    vec3   tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3   bitangent = cross(normal, tangent);
    mat3   TBN       = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    float bias      = max(0.025, 0.05 * (1.0 - dot(normal, normalize(-fragPos))));

    for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
    {
        vec3 kernelSample = ssaoData.ssaoKernels[i].xyz;
        vec3 samplePos    = fragPos + (TBN * kernelSample) * SSAO_RADIUS;

        vec4 offset  = ubo.projection * vec4(samplePos, 1.0);
        offset.xyz  /= offset.w;
        offset.x     =  offset.x * 0.5 + 0.5;
        offset.y     = -offset.y * 0.5 + 0.5;

        float sampledDepth = texture(sampler2D(depthRT, rtSampler1), offset.xy).r;
        vec3  sampledPos   = reconstructPosition(offset.xy, sampledDepth, invProj);

        float rangeCheck = smoothstep(1.0, 0.0, abs(fragPos.z - sampledPos.z) / SSAO_RADIUS);
        occlusion += (sampledPos.z <= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    outOcclusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
}
