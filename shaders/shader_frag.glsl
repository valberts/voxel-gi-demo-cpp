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
layout(location = 6) uniform int shadingMode;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

layout(location = 0) out vec4 fragColor;

void main()
{
    const vec3 lightPos = vec3(0.0, 10.0, 10.0);
    const vec3 lightColor = vec3(1.0, 1.0, 1.0); 
    vec3 lightDir = normalize(lightPos - fragPosition);
    
    vec3 diffuse = max(dot(fragNormal, lightDir), 0.0) * lightColor;

    if (shadingMode == 0) {
        fragColor = vec4(diffuse, 1.0);
    } else if (shadingMode == 1) {
        fragColor = vec4(fragPosition, 1.0);
    } else {
        fragColor = vec4(0.0);
    }
}
