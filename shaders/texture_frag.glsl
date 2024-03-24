#version 450
out vec4 FragColor;

in vec2 TexCoords;

layout(location = 3) uniform sampler2D textureAtlas;

void main() {
    FragColor = texture(textureAtlas, TexCoords);
}
