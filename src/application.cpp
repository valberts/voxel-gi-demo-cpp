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
#include "voxel_grid.cpp"

// The Application class encapsulates the entire application, including setup, event handling, and rendering.
class Application {
public:
    // Application constructor: initializes the application, creating a window and loading resources.
    Application()
        : m_window("Voxel GI Demo", glm::ivec2(1024, 1024), OpenGLVersion::GL45) // setup window with dimensions 1024x1024 and OpenGL 4.5
        , m_camera(&m_window, glm::vec3(-1.44f, 0.5f, 2.3f), glm::vec3(0.5f, -0.2f, -0.8f), 0.03f, 0.0035f) // setup camera with position, forward, move speed, and look speed
        , m_texture("resources/checkerboard.png") // load an image texture from resources
    {
        setupInputCallbacks();
        loadMeshes();
        loadShaders();

        setupAtlasShader();
        setupTextureShader();
        setupDebugShader();
    }

    // Main game/rendering loop
    void update()
    {
        while (!m_window.shouldClose()) {
            // This is your game loop
            // Put your real-time logic and rendering in here
            processInput();
            renderScene();
            // Processes input and swaps the window buffer
            m_window.swapBuffers();
        }
    }

    void processInput() {
        static glm::vec3 lastTranslation(0.0f);
        static glm::vec3 translation(0.0f);
        const std::vector<int> atlasSizes = { 22, 44, 88, 176, 368, 768, 1280  };
        m_window.updateInput();
        m_camera.updateInput();
        //std::cout << m_camera.toString() << std::endl; // debug camera position and forward if needed

        // Use ImGui for easy input/output of ints, floats, strings, etc...
        ImGui::Begin("Window");
        ImGui::InputInt("Shading mode", &m_shadingMode); // Use ImGui::DragInt or ImGui::DragFloat for larger range of numbers.
        ImGui::InputInt("Render mode", &m_renderMode); // Use ImGui::DragInt or ImGui::DragFloat for larger range of numbers.

        ImGui::Text("Atlas Length");
        ImGui::SameLine();
        if (ImGui::Button("-")) {
            auto it = std::find(atlasSizes.begin(), atlasSizes.end(), atlasLength);
            if (it != atlasSizes.end() && it != atlasSizes.begin()) {
                atlasLength = *(--it);
                resetAtlasTexture();
                recalculateVoxelGrid();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+")) {
            auto it = std::find(atlasSizes.begin(), atlasSizes.end(), atlasLength);
            if (it != atlasSizes.end() && (it + 1) != atlasSizes.end()) {
                atlasLength = *(++it);
                resetAtlasTexture();
                recalculateVoxelGrid();
            }
        }

        ImGui::SameLine();
        ImGui::Text("%d", atlasLength);

        ImGui::Text("Voxel Grid Length");
        ImGui::SameLine();
        if (ImGui::Button("/2")) {
            m_voxelGrid.gridLength /= 2;
            recalculateVoxelGrid();
        }
        ImGui::SameLine();
        if (ImGui::Button("*2")) {
            m_voxelGrid.gridLength *= 2;
            recalculateVoxelGrid();
        }

        ImGui::SameLine();
        ImGui::Text("%d", m_voxelGrid.gridLength);

        ImGui::Text("Translation");

        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine(); // Keep buttons on the same line

            std::string id(1, "XYZ"[i]); // Label for axis
            if (ImGui::Button(("-" + id).c_str())) {
                translation[i] -= 0.1f;
            }
            ImGui::SameLine();
            if (ImGui::Button(("+" + id).c_str())) {
                translation[i] += 0.1f;
            }

            // Display current value
            ImGui::SameLine();
            ImGui::Text("%.1f", translation[i]);
        }

        // Check if translation has changed since last frame
        if (translation != lastTranslation) {
            m_modelMatrix = glm::translate(glm::mat4(1.0f), translation);
            recalculateVoxelGrid();
        }
        lastTranslation = translation;

        //ImGui::Text("Value is: %i", dummyInteger); // Use C printf formatting rules (%i is a signed integer)
        ImGui::Checkbox("Show atlas", &m_showAtlas);
        ImGui::Checkbox("Show voxel grid bounds", &m_showDebug);
        //ImGui::Checkbox("Use material if no texture", &m_useMaterial);
        ImGui::End();
    }

    void renderScene() {
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

        // Voxel stuff
        std::vector<glm::vec3> texData(atlasLength * atlasLength);

        for (GPUMesh& mesh : m_meshes) {
            // Bind the shader program that will be used for rendering. This tells OpenGL to use the shader's vertex and fragment shaders for drawing commands
            if (validTexels.empty()) {
                std::cout << "Rendering world positions to atlas texture." << std::endl;
                renderAtlas(mesh, normalModelMatrix);

                // bind the atlas texture
                glBindTexture(GL_TEXTURE_2D, atlasTexture);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, texData.data());

                std::cout << "Searching for valid texels." << std::endl;
                // Iterate through the texture data to find valid texels.
                for (int y = 0; y < atlasLength; ++y) {
                    for (int x = 0; x < atlasLength; ++x) {
                        glm::vec3 worldPos = texData[y * atlasLength + x];
                        // INVALID_COLOR used to mark invalid texels
                        if (worldPos != glm::vec3(INVALID_COLOR)) {
                            validTexels.push_back(worldPos);
                        }
                    }
                }
            }

            // setup model matrices
            if (modelMatrices.empty()) {
                std::cout << "Populating model matrices with voxel positions" << std::endl;
                for (const auto& worldPos : validTexels) {
                    glm::ivec3 gridPos = m_voxelGrid.worldToGridPosition(worldPos);

                    if (!m_voxelGrid.isGridPositionOccupied(gridPos)) {
                        glm::mat4 modelMatrix = glm::mat4(1.0f);
                        glm::vec3 adjustedWorldPos = m_voxelGrid.gridToWorldPosition(gridPos);
                        modelMatrix = glm::translate(modelMatrix, adjustedWorldPos);
                        modelMatrix = glm::scale(modelMatrix, glm::vec3(m_voxelGrid.voxelScale)); // scale to fit grid
                        modelMatrices.push_back(modelMatrix); // add model matrix for this cube to the vector
                        m_voxelGrid.occupiedPositions.push_back(gridPos); // mark position as occupied
                    }
                }
            }

            // prepare voxel instancing
            if (!modelMatrices.empty() && !voxelsReady) {
                std::cout << "Setting up instanced rendering with model matrices" << std::endl;
                setupVoxelInstancing();
            }

            if (m_renderMode == 0) {
                renderMesh(mesh, mvpMatrix, normalModelMatrix);
            }

            if (voxelsReady && m_renderMode == 1) {
                renderVoxels();
            }
        }

        // Other rendering
        if (m_showAtlas) {
            renderAtlasToQuad();
        }

        if (m_showDebug) {
            renderDebug();
        }
    }

    void renderMesh(GPUMesh& mesh, glm::mat4 mvpMatrix, glm::mat4 normalModelMatrix) {
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
            glUniform1i(6, m_shadingMode);
            glUniform3fv(7, 1, glm::value_ptr(m_lightPos));
        }
        else {
            glUniform1i(4, GL_FALSE);
            glUniform1i(5, m_useMaterial);
            glUniform1i(6, m_shadingMode);
            glUniform3fv(7, 1, glm::value_ptr(m_lightPos));
        }
        mesh.draw(m_defaultShader);
    }

    void renderVoxels() {
        glBindVertexArray(voxelGridVAO);
        m_voxelShader.bind();
        glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(m_projectionMatrix));
        glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(m_camera.viewMatrix()));
        glUniform1i(3, m_shadingMode);
        glUniform3fv(4, 1, glm::value_ptr(m_lightPos));
        glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0, modelMatrices.size());

        glBindVertexArray(0);
    }

    void renderAtlas(GPUMesh& mesh, glm::mat4 normalModelMatrix) {
        // Do all atlas rendering here
        glBindFramebuffer(GL_FRAMEBUFFER, atlasFBO);
        glViewport(0, 0, atlasLength, atlasLength);
        glClearColor(INVALID_COLOR, INVALID_COLOR, INVALID_COLOR, INVALID_COLOR);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_atlasShader.bind();
        // Pass the Model-View-Projection matrix to the vertex shader
        glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(m_projectionMatrix * m_viewMatrix * m_modelMatrix));
        // The object's transform. Multiplying a vertex by the model matrix transforms the vector into world space.
        glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
        glUniformMatrix3fv(2, 1, GL_FALSE, glm::value_ptr(normalModelMatrix));
        glUniform1i(4, GL_FALSE);
        glUniform1i(5, m_useMaterial);
        mesh.draw(m_atlasShader);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // reset viewport
        glViewport(0, 0, m_window.getWindowSize().x, m_window.getWindowSize().y);
    }

    void renderAtlasToQuad() {
        // Texture shader stuff
        m_textureShader.bind();
        glBindVertexArray(quadVAO);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);
        glUniform1i(3, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void renderDebug() {
        m_lineShader.bind();

        glm::mat4 mvp = m_projectionMatrix * m_camera.viewMatrix() * glm::mat4(1.0f);
        glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));

        glBindVertexArray(lineVAO);
        glDrawArrays(GL_LINES, 0, 24);
        glBindVertexArray(0);
    }

    void recalculateVoxelGrid() {
        m_voxelGrid.clearGrid();
        m_voxelGrid.calculateVoxelScale();
        validTexels.clear();
        modelMatrices.clear();
        voxelsReady = false;
    }

    // In here you can handle key presses
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyPressed(int key, int mods)
    {
        //std::cout << "Key pressed: " << key << std::endl;
    }

    // In here you can handle key releases
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyReleased(int key, int mods)
    {
        //std::cout << "Key released: " << key << std::endl;
    }

    // If the mouse is moved this function will be called with the x, y screen-coordinates of the mouse
    void onMouseMove(const glm::dvec2& cursorPos)
    {
        //std::cout << "Mouse at position: " << cursorPos.x << " " << cursorPos.y << std::endl;
    }

    // If one of the mouse buttons is pressed this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseClicked(int button, int mods)
    {
        //std::cout << "Pressed mouse button: " << button << std::endl;
    }

    // If one of the mouse buttons is released this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseReleased(int button, int mods)
    {
        //std::cout << "Released mouse button: " << button << std::endl;
    }

private:
    Window m_window;
    Camera m_camera;
    VoxelGrid m_voxelGrid;

    // Shader for default rendering and for depth rendering
    Shader m_defaultShader;
    Shader m_shadowShader;
    Shader m_atlasShader;
    Shader m_textureShader;
    Shader m_lineShader;
    Shader m_voxelShader;

    std::vector<GPUMesh> m_meshes;
    Texture m_texture;
    // State
    int m_renderMode{ 0 }; // 0 = render models, 1 = render voxels
    int m_shadingMode{ 0 }; // 0 = diffuse, 1 = world position
    bool m_showAtlas{ false }; // whether or not to show world pos atlas
    bool m_showDebug{ false }; // whether or not to show debug voxel grid boundaries
    bool m_useMaterial{ true };

    // Projection and view matrices for you to fill in and use
    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_viewMatrix = glm::lookAt(glm::vec3(-1, 1, -1), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 m_modelMatrix { 1.0f };
    glm::vec3 m_lightPos{ 0.0f, 10.0f, 10.0f };

    // Atlas variables
    GLuint atlasFBO, atlasTexture;
    int atlasLength = 176; // paper uses 176x176 minimum
    const float INVALID_COLOR = 0.4f;

    // Texture variables
    GLuint quadVAO, quadVBO;

    // Voxel variables
    std::vector<glm::vec3> validTexels; // stores valid world positions read from atlas texture
    std::vector<glm::mat4> modelMatrices;
    unsigned int instanceVBO;
    unsigned int voxelGridVAO, voxelGridVBO, voxelGridEBO;
    bool voxelsReady = false;

    // debug variables
    GLuint lineVAO, lineVBO;
    std::vector<glm::vec3> lineVertices;

    void setupInputCallbacks() {
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
    }

    void loadMeshes() {
        // Load the 3D model into GPU memory.
        m_meshes = GPUMesh::loadMeshGPU("resources/bunny.obj");
    }

    void loadShaders() {
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

            ShaderBuilder lineBuilder;
            lineBuilder.addStage(GL_VERTEX_SHADER, "shaders/line_vert.glsl");
            lineBuilder.addStage(GL_FRAGMENT_SHADER, "shaders/line_frag.glsl");
            m_lineShader = lineBuilder.build();

            ShaderBuilder voxelBuilder;
            voxelBuilder.addStage(GL_VERTEX_SHADER, "shaders/voxel_vert.glsl");
            voxelBuilder.addStage(GL_FRAGMENT_SHADER, "shaders/voxel_frag.glsl");
            m_voxelShader = voxelBuilder.build();

            // Any new shaders can be added below in similar fashion.
            // ==> Don't forget to reconfigure CMake when you do!
            //     Visual Studio: PROJECT => Generate Cache for ComputerGraphics
            //     VS Code: ctrl + shift + p => CMake: Configure => enter
            // ....

            // Note: Shaders must be compiled and linked successfully. Handle exceptions accordingly.
        }
        catch (ShaderLoadingException e) {
            std::cerr << e.what() << std::endl;
        }
    }

    void setupAtlasShader() {
        // Setup the atlas shader, this will render the world positions of the mesh to a UV unwrapped texture
        glCreateFramebuffers(1, &atlasFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, atlasFBO);

        glCreateTextures(GL_TEXTURE_2D, 1, &atlasTexture);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, atlasLength, atlasLength, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, atlasTexture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void resetAtlasTexture() {
        if (atlasTexture) {
            glDeleteTextures(1, &atlasTexture);
            glDeleteFramebuffers(1, &atlasFBO);
            setupAtlasShader();
        }
    }

    void setupTextureShader() {
        // Setup the shader for rendering the atlas texture to a quad for demonstration purposes

        float scale = 0.5f; // scale of the quad
        // how much to translate quad to move it to the top left corner of the screen
        float translateX = -1.0f + (scale * 1.0f);
        float translateY = 1.0f - (scale * 1.0f); 

        float quadVertices[] = {
            // positions (scaled and translated)     // texCoords
            (-1.0f * scale + translateX),  (1.0f * scale + translateY),  0.0f, 1.0f,
            (-1.0f * scale + translateX),  (0.0f * scale + translateY),  0.0f, 0.0f,
            (0.0f * scale + translateX),  (0.0f * scale + translateY),  1.0f, 0.0f,

            (-1.0f * scale + translateX),  (1.0f * scale + translateY),  0.0f, 1.0f,
            (0.0f * scale + translateX),  (0.0f * scale + translateY),  1.0f, 0.0f,
            (0.0f * scale + translateX),  (1.0f * scale + translateY),  1.0f, 1.0f
        };


        glCreateVertexArrays(1, &quadVAO);
        glCreateBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    }

    void setupDebugShader() {
        // Setup the shader for showing the bounding box of the voxel grid

        glCreateVertexArrays(1, &lineVAO);
        glCreateBuffers(1, &lineVBO);
        glBindVertexArray(lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, lineVBO);

        // define vertices of voxel grid's bounding box
        glm::vec3 boxVertices[] = {
            // Bottom square
            glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMin.z), glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMin.z),
            glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMin.z), glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMax.z),
            glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMax.z), glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMax.z),
            glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMax.z), glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMin.z),
            // Top square
            glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMin.z), glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMin.z),
            glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMin.z), glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMax.z),
            glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMax.z), glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMax.z),
            glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMax.z), glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMin.z),
            // Vertical lines connecting top and bottom
            glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMin.z), glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMin.z),
            glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMin.z), glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMin.z),
            glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMax.z), glm::vec3(m_voxelGrid.worldMax.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMax.z),
            glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMin.y, m_voxelGrid.worldMax.z), glm::vec3(m_voxelGrid.worldMin.x, m_voxelGrid.worldMax.y, m_voxelGrid.worldMax.z)
        };


        glBufferData(GL_ARRAY_BUFFER, sizeof(boxVertices), &boxVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void setupVoxelInstancing() {
        // Render voxels once this is ready to be rendered
        // Using instanced cubes to draw them all in one go and speed up rendering
        // vertices for each cube representing a voxel in the voxel grid
        float vertices[] = {
            // Front face
            -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f, // 0
             0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f, // 1
             0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f, // 2
            -0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f, // 3
            // Right face
             0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f, // 4
             0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f, // 5
             0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f, // 6
             0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f, // 7
             // Back face
             -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f, // 8
              0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f, // 9
              0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f, // 10
             -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f, // 11
             // Left face
             -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, // 12
             -0.5f, -0.5f,  0.5f, -1.0f, 0.0f, 0.0f, // 13
             -0.5f,  0.5f,  0.5f, -1.0f, 0.0f, 0.0f, // 14
             -0.5f,  0.5f, -0.5f, -1.0f, 0.0f, 0.0f, // 15
             // Top face
             -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f, // 16
              0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f, // 17
              0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f, // 18
             -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f, // 19
             // Bottom face
             -0.5f, -0.5f, -0.5f,  0.0f, -1.0f, 0.0f, // 20
              0.5f, -0.5f, -0.5f,  0.0f, -1.0f, 0.0f, // 21
              0.5f, -0.5f,  0.5f,  0.0f, -1.0f, 0.0f, // 22
             -0.5f, -0.5f,  0.5f,  0.0f, -1.0f, 0.0f  // 23
        };


        // Cube indices
        unsigned int indices[] = {
            // Front face
            0, 1, 2,  2, 3, 0,
            // Right face
            4, 5, 6,  6, 7, 4,
            // Back face
            8, 9, 10,  10, 11, 8,
            // Left face
            12, 13, 14,  14, 15, 12,
            // Top face
            16, 17, 18,  18, 19, 16,
            // Bottom face
            20, 21, 22,  22, 23, 20
        };

        glCreateVertexArrays(1, &voxelGridVAO);
        glBindVertexArray(voxelGridVAO);

        // Vertex Buffer Object (VBO) for vertex positions
        glCreateBuffers(1, &voxelGridVBO);
        glBindBuffer(GL_ARRAY_BUFFER, voxelGridVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // Position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Normal attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // Element Buffer Object (EBO) for indices
        glCreateBuffers(1, &voxelGridEBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, voxelGridEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        // Instance Buffer Object for model matrices
        glCreateBuffers(1, &instanceVBO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, modelMatrices.size() * sizeof(glm::mat4), modelMatrices.data(), GL_STATIC_DRAW);

        // set attribute pointes for instance matrix (4 vec4s, since mat4 is just 4 vec4s)
        size_t vec4Size = sizeof(glm::vec4);
        for (int i = 0; i < 4; i++) {
            glEnableVertexAttribArray(3 + i); // locations 3-6
            glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, 4 * vec4Size, (void*)(i * vec4Size));
            glVertexAttribDivisor(3 + i, 1); // only updates per instance
        }

        glBindVertexArray(0);

        voxelsReady = true;
        std::cout << "Voxels ready to be rendered!" << std::endl;
    }
};

int main()
{
    Application app;
    app.update();

    return 0;
}
