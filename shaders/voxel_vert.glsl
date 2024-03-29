#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in mat4 modelMatrix; // passed as attribute

layout(location = 0) uniform mat4 projectionMatrix;
layout(location = 1) uniform mat4 viewMatrix;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;

void main()
{
    mat4 mvpMatrix = projectionMatrix * viewMatrix * modelMatrix;
    gl_Position = mvpMatrix * vec4(position, 1.0);
    
    fragPosition = vec3(modelMatrix * vec4(position, 1.0));
    fragNormal = mat3(transpose(inverse(modelMatrix))) * normal; // compute normal matrix
    fragTexCoord = texCoord;
}
