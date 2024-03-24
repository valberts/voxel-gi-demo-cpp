#version 450

layout(std140) uniform Material // Must match the GPUMaterial defined in src/mesh.h
{
    vec3 kd;
	vec3 ks;
	float shininess;
	float transparency;
};

layout(location = 3) uniform sampler2D colorMap;
layout(location = 4) uniform bool hasTexCoords;
layout(location = 5) uniform bool useMaterial;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec3 worldPosition;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(fragPosition, 1.0); // not sure if we use fragPosition or worldPosition, but it might just be identical
}
