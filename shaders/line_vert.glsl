#version 450 core
layout(location = 0) in vec3 aPos;

layout(location = 0) uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
