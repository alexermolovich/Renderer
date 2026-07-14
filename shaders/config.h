#ifndef SHADER_CONFIG_H
#define SHADER_CONFIG_H

// Shadows
#define MAX_SHADOW_CASCADES   4
#define SHADOW_MAP_SIZE       2048
#define int32 int
// Lights
#define MAX_POINT_LIGHTS      6
#define MAX_DIRECTIONAL_LIGHT 5

struct ShadowCascadeData {
    mat4 viewProj;
    float splitDepth;
};

struct ShadowGenerationUniform {
    ShadowCascadeData cascades[MAX_SHADOW_CASCADES];
    vec4 lightDirection;
    vec4 shadowMapSizeCascadeCount;
    vec4 bias;
};

struct SkyLightUniform {

    vec3 lightDirection;
    vec3 lightColor;
    float lightIntensity;

};

struct PointLightPushConstants {
    vec4 positionRadius;
    vec4 colorIntensity;
};

struct DirectionalLightPushConstants {
    vec4 directionIntensity;
    vec4 color;
};

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

struct DirectionalLight {
    vec4 directionIntensity;
    vec4 color;
};

// Compose push constants for light calculation.
struct ComposePushConstants {

    DirectionalLightPushConstants directionalLight[MAX_DIRECTIONAL_LIGHT];
    int32 active_directionalLight;

    PointLightPushConstants pointLight[MAX_POINT_LIGHTS];
    int32 active_pointlight;

};


#endif
