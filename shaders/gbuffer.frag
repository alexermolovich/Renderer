#version 450
#extension GL_EXT_nonuniform_qualifier : enable

#define MESH_TEXTURE_COUNT 255

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) flat in uint inTextureIndex;
 
layout (location = 0) out vec4 outNormalSpecular;
layout (location = 1) out vec4 outAlbedo;

layout (set = 1, binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
    vec3 cameraPos;
	float nearPlane;
	float farPlane;
} ubo;

layout(set = 0, binding = 5) uniform texture2D meshTextures[MESH_TEXTURE_COUNT];
layout(set = 0, binding = 8) uniform sampler textureSampler;

vec2 encodeNormalOct(vec3 n)
{

  n /= abs(n.x) + abs(n.y) + abs(n.z);
	
  vec2 enc = n.xy;

  if (n.z < 0.0)
	{
		enc = (1.0 - abs(enc.yx)) * sign(enc.xy);
	}
	return enc * 0.5 + 0.5;
}

void main() 
{
	outNormalSpecular = vec4(encodeNormalOct(normalize(inNormal)), 0.0, 1.0);
	uint textureIndex = min(inTextureIndex, uint(MESH_TEXTURE_COUNT - 1));
	vec4 baseColor = texture(sampler2D(meshTextures[nonuniformEXT(textureIndex)], textureSampler), inUV);
	outAlbedo = vec4(baseColor.rgb, 1.0);
}
