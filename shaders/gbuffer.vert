#version 450

layout(location = 0) in vec3 inPos;  
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;      

layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;

layout (set =1, binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
    vec3 cameraPos;
} ubo;


void main() 
{
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPos, 1.0);
	
	outUV = inUV;

	outPos = vec3(ubo.view * ubo.model * vec4(inPos, 1.0));

	mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));
	outNormal = normalMatrix * inNormal;

}
