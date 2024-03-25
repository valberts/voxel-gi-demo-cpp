//#include "Image.h"
#include "mesh.h"
#include "texture.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
// Can't wait for modules to fix this stuff...
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
// Include glad before glfw3
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <functional>
#include <iostream>
#include <vector>
#include "camera.h"

// The Application class encapsulates the entire application, including setup, event handling, and rendering.
class Application {
public:
    // Application constructor: initializes the application, creating a window and loading resources.
    Application()
        : m_window("Voxel GI Demo", glm::ivec2(1024, 1024), OpenGLVersion::GL45) // setup window with dimensions 1024x1024 and OpenGL 4.5
        , m_camera(&m_window, glm::vec3(-1.0f, 0.2f, -0.5f), glm::vec3(1.0f, 0.0f, 0.4f), 0.03f, 0.0035f) // setup camera with position, forward, move speed, and look speed
        , m_texture("resources/checkerboard.png") // load an image texture from resources
    {
           
        // Register input and call appropriate methods
        m_window.registerKeyCallback([this](int key, int scancode, int action, int mods) {
            if (action == GLFW_PRESS)
                onKeyPressed(key, mods);
            else if (action == GLFW_RELEASE)
                onKeyReleased(key, mods);
        });
        m_window.registerMouseMoveCallback(std::bind(&Application::onMouseMove, this, std::placeholders::_1));
        m_window.registerMouseButtonCallback([this](int button, int action, int mods) {
            if (action == GLFW_PRESS)
                onMouseClicked(button, mods);
            else if (action == GLFW_RELEASE)
                onMouseReleased(button, mods);
        });

        // Load the 3D model into GPU memory.
        m_meshes = GPUMesh::loadMeshGPU("resources/bunny.obj");
        // Load the cube model into GPU memory.
        m_cubeMesh = GPUMesh::loadMeshGPU("resources/cube.obj");

        // Setup shaders for rendering, including vertex and fragment shaders for default and shadow effects.
        try {
            ShaderBuilder defaultBuilder;
            defaultBuilder.addStage(GL_VERTEX_SHADER, "shaders/shader_vert.glsl");
            defaultBuilder.addStage(GL_FRAGMENT_SHADER, "shaders/shader_frag.glsl");
            m_defaultShader = defaultBuilder.build();

            ShaderBuilder shadowBuilder;
            shadowBuilder.addStage(GL_VERTEX_SHADER, "shaders/shadow_vert.glsl");
            m_shadowShader = shadowBuilder.build();

            ShaderBuilder atlasBuilder;
            atlasBuilder.addStage(GL_VERTEX_SHADER, "shaders/atlas_vert.glsl");
            atlasBuilder.addStage(GL_FRAGMENT_SHADER, "shaders/atlas_frag.glsl");
            m_atlasShader = atlasBuilder.build();

            ShaderBuilder textureBuilder;
            textureBuilder.addStage(GL_VERTEX_SHADER, "shaders/texture_vert.glsl");
            textureBuilder.addStage(GL_FRAGMENT_SHADER, "shaders/texture_frag.glsl");
            m_textureShader = textureBuilder.build();

            // Any new shaders can be added below in similar fashion.
            // ==> Don't forget to reconfigure CMake when you do!
            //     Visual Studio: PROJECT => Generate Cache for ComputerGraphics
            //     VS Code: ctrl + shift + p => CMake: Configure => enter
            // ....

            // Note: Shaders must be compiled and linked successfully. Handle exceptions accordingly.
        } catch (ShaderLoadingException e) {
            std::cerr << e.what() << std::endl;
        }

        // Here we bind the atlas shader and then do all the stuff for that
        glCreateFramebuffers(1, &atlasFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, atlasFBO);

        glCreateTextures(GL_TEXTURE_2D, 1, &atlasTexture);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, atlasWidth, atlasHeight, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, atlasTexture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Texture shader
        
        float scale = 0.5f; // Adjust this value to scale the quad size
        // Calculate the translation needed to move the scaled quad to the top left corner
        float translateX = -1.0f + (scale * 1.0f); // Move it left to the edge, then adjust for scaling
        float translateY = 1.0f - (scale * 1.0f); // Move it up to the edge, then adjust for scaling

        float quadVertices[] = {
            // positions (scaled and translated)     // texCoords
            (-1.0f * scale + translateX),  (1.0f * scale + translateY),  0.0f, 1.0f,
            (-1.0f * scale + translateX),  (0.0f * scale + translateY),  0.0f, 0.0f,
            (0.0f * scale + translateX),  (0.0f * scale + translateY),  1.0f, 0.0f,

            (-1.0f * scale + translateX),  (1.0f * scale + translateY),  0.0f, 1.0f,
            (0.0f * scale + translateX),  (0.0f * scale + translateY),  1.0f, 0.0f,
            (0.0f * scale + translateX),  (1.0f * scale + translateY),  1.0f, 1.0f
        };


        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    }

    // Main game/rendering loop
    void update()
    {
        int dummyInteger = 0; // Initialized to 0
        while (!m_window.shouldClose()) {
            // This is your game loop
            // Put your real-time logic and rendering in here
            m_window.updateInput();
            m_camera.updateInput();
            //std::cout << m_camera.toString() << std::endl; // debug camera position and forward if needed

            // Use ImGui for easy input/output of ints, floats, strings, etc...
            ImGui::Begin("Window");
            ImGui::InputInt("This is an integer input", &dummyInteger); // Use ImGui::DragInt or ImGui::DragFloat for larger range of numbers.
            ImGui::Text("Value is: %i", dummyInteger); // Use C printf formatting rules (%i is a signed integer)
            ImGui::Checkbox("Use material if no texture", &m_useMaterial);
            ImGui::End();

            // Clear the screen
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // ...
            glEnable(GL_DEPTH_TEST);

            // Calculate MVP (Model-View-Projection) matrix for transforming vertices.
            const glm::mat4 mvpMatrix = m_projectionMatrix * m_camera.viewMatrix() * m_modelMatrix;
            // Normals should be transformed differently than positions (ignoring translations + dealing with scaling):
            // https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html

            // Calculate a matrix for transforming normals properly.
            const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(m_modelMatrix));

            // Render each mesh in the scene.
            for (GPUMesh& mesh : m_meshes) {
                // Bind the shader program that will be used for rendering. This tells OpenGL to use the shader's vertex and fragment shaders for drawing commands.
                
                m_defaultShader.bind();
                // Pass the Model-View-Projection matrix to the vertex shader. This transforms vertices from model space to clip space.
                // https://jsantell.com/model-view-projection/
                glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvpMatrix));
                // The object's translation, rotation, and scsale transform. Multiplying a vertex by the model matrix transforms the vector into world space.
                glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
                glUniformMatrix3fv(2, 1, GL_FALSE, glm::value_ptr(normalModelMatrix));
                
                // Check if the current mesh has texture coordinates. This determines if a texture will be used for rendering.

                if (mesh.hasTextureCoords()) {
                    m_texture.bind(GL_TEXTURE0);
                    glUniform1i(3, 0);
                    glUniform1i(4, GL_TRUE);
                    glUniform1i(5, GL_FALSE);
                } else {
                    glUniform1i(4, GL_FALSE);
                    glUniform1i(5, m_useMaterial);
                }
                mesh.draw(m_defaultShader);

                // Do all atlas rendering here
                glBindFramebuffer(GL_FRAMEBUFFER, atlasFBO);
                glViewport(0, 0, atlasWidth, atlasHeight);
                glClearColor(INVALID_COLOR, INVALID_COLOR, INVALID_COLOR, INVALID_COLOR);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                
                m_atlasShader.bind();
                // Pass the Model-View-Projection matrix to the vertex shader. This transforms vertices from model space to clip space.
                glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(m_projectionMatrix * m_viewMatrix * m_modelMatrix));
                // The object's translation, rotation, and scsale transform. Multiplying a vertex by the model matrix transforms the vector into world space.
                glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
                glUniformMatrix3fv(2, 1, GL_FALSE, glm::value_ptr(normalModelMatrix));
                glUniform1i(4, GL_FALSE);
                glUniform1i(5, m_useMaterial);
                mesh.draw(m_atlasShader);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // reset viewport
                glViewport(0, 0, m_window.getWindowSize().x, m_window.getWindowSize().y);

                // Texture shader stuff
                m_textureShader.bind();
                glBindVertexArray(quadVAO);
                glBindTexture(GL_TEXTURE_2D, atlasTexture); // Ensure this is the texture you want to display
                glUniform1i(3, 0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);

                // Voxel stuff
                std::vector<glm::vec3> validTexels; // This will store the valid world positions read from the texture.
                int texWidth = atlasWidth, texHeight = atlasHeight;
                std::vector<glm::vec3> texData(texWidth * texHeight);

                // Bind the texture to ensure we're reading the right one.
                glBindTexture(GL_TEXTURE_2D, atlasTexture);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, texData.data());

                // Iterate through the texture data to find valid texels.
                for (int y = 0; y < texHeight; ++y) {
                    for (int x = 0; x < texWidth; ++x) {
                        glm::vec3 worldPos = texData[y * texWidth + x];
                        // INVALID_COLOR used to mark invalid texels
                        if (worldPos != glm::vec3(INVALID_COLOR)) {
                            validTexels.push_back(worldPos);

                            // Calculate the model matrix for the cube
                            glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), worldPos);
                            // Assuming uniform scaling of cubes, if needed
                            modelMatrix = glm::scale(modelMatrix, glm::vec3(0.04f)); // Scale down the cube

                            // Now render the cube
                            m_defaultShader.bind();
                            glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(m_projectionMatrix * m_camera.viewMatrix() * modelMatrix));
                            glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(modelMatrix));
                            // Since cubes are simple, normals might not need adjustment, but here's how you could calculate the matrix if needed.
                            glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(modelMatrix));
                            glUniformMatrix3fv(2, 1, GL_FALSE, glm::value_ptr(normalModelMatrix));

                            // Assume cubes have texture coordinates and a uniform material
                            glUniform1i(4, GL_FALSE);
                            glUniform1i(5, m_useMaterial);
                            for (GPUMesh& mesh : m_cubeMesh) {
                                mesh.draw(m_defaultShader);
                            }

                        }
                    }
                }

            }

            // Processes input and swaps the window buffer
            m_window.swapBuffers();
        }
    }

    // In here you can handle key presses
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyPressed(int key, int mods)
    {
        std::cout << "Key pressed: " << key << std::endl;
    }

    // In here you can handle key releases
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyReleased(int key, int mods)
    {
        std::cout << "Key released: " << key << std::endl;
    }

    // If the mouse is moved this function will be called with the x, y screen-coordinates of the mouse
    void onMouseMove(const glm::dvec2& cursorPos)
    {
        std::cout << "Mouse at position: " << cursorPos.x << " " << cursorPos.y << std::endl;
    }

    // If one of the mouse buttons is pressed this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseClicked(int button, int mods)
    {
        std::cout << "Pressed mouse button: " << button << std::endl;
    }

    // If one of the mouse buttons is released this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseReleased(int button, int mods)
    {
        std::cout << "Released mouse button: " << button << std::endl;
    }

private:
    Window m_window;

    Camera m_camera;

    // Shader for default rendering and for depth rendering
    Shader m_defaultShader;
    Shader m_shadowShader;
    Shader m_atlasShader;
    Shader m_textureShader;

    std::vector<GPUMesh> m_meshes;
    std::vector<GPUMesh> m_cubeMesh;
    Texture m_texture;
    bool m_useMaterial { true };

    // Projection and view matrices for you to fill in and use
    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_viewMatrix = glm::lookAt(glm::vec3(-1, 1, -1), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 m_modelMatrix { 1.0f };

    // Atlas variables
    GLuint atlasFBO, atlasTexture;
    const int atlasWidth = 32; // paper uses 176x176 minimum
    const int atlasHeight = 32;
    const float INVALID_COLOR = 0.4f;

    // Texture variables
    GLuint quadVAO, quadVBO;

    // Voxel variables
    const int gridWidth = 64, gridHeight = 64, gridDepth = 128, voxelSize = 0.005f;
};

int main()
{
    Application app;
    app.update();

    return 0;
}
