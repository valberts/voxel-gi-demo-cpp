#include "mesh.h"
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <tinyobjloader/tiny_obj_loader.h>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cassert>
#include <exception>
#include <iostream>
#include <numeric>
#include <span>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>

static void centerAndScaleToUnitMesh(std::span<Mesh> meshes);

static glm::vec3 construct_vec3(const float* pFloats)
{
    return glm::vec3(pFloats[0], pFloats[1], pFloats[2]);
}

// https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
template <class T>
static void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct VertexHash {
    size_t operator()(const Vertex& v) const
    {
        size_t seed = 0;
        hash_combine(seed, v.position.x);
        hash_combine(seed, v.position.y);
        hash_combine(seed, v.position.z);
        hash_combine(seed, v.normal.x);
        hash_combine(seed, v.normal.y);
        hash_combine(seed, v.normal.z);
        hash_combine(seed, v.texCoord.s);
        hash_combine(seed, v.texCoord.t);
        return seed;
    }
};

// Defines a structure to uniquely identify vertices.
struct VertexKey {
    int vertexIndex; // Index of the vertex's position in the model's vertex list.
    int normalIndex; // Index of the vertex's normal in the model's normal list.
    int texcoordIndex; // Index of the vertex's texture coordinate in the model's texture coordinate list

    // Overloads the equality operator to compare two VertexKeys.
    // Returns true if both VertexKeys have the same vertex, normal, and texture coordinate indices.
    bool operator==(const VertexKey& other) const
    {
        return vertexIndex == other.vertexIndex && normalIndex == other.normalIndex && texcoordIndex == other.texcoordIndex;
    }
};

// Extends the std namespace to include a specialization of the std::hash template for the VertexKey struct.
namespace std {
    template <> struct hash<VertexKey> {
        // Implements the hash function for VertexKey.
        size_t operator()(const VertexKey& key) const {
            size_t seed = 0;
            hash_combine(seed, key.vertexIndex);
            hash_combine(seed, key.normalIndex);
            hash_combine(seed, key.texcoordIndex);
            return seed; // Returns the final hash value generated from the vertex, normal, and texture coordinate indices.
        }
    };
}

// The issue is, tinyObjIndex.vertex_index is the position index and then we look up whether there is a vertex in that position.
// but the previous implementation did not consider that there will have to be multiple vertices with the same position but different texcoords and normals
std::vector<Mesh> loadMesh(const std::filesystem::path& file, bool centerAndNormalize)
{
    if (!std::filesystem::exists(file)) {
        std::cerr << "File " << file << " does not exist." << std::endl;
        throw std::exception();
    }

    const auto baseDir = file.parent_path();

    tinyobj::attrib_t inAttrib;
    std::vector<tinyobj::shape_t> inShapes;
    std::vector<tinyobj::material_t> inMaterials;

    std::string warn, error;
    bool ret = tinyobj::LoadObj(&inAttrib, &inShapes, &inMaterials, &warn, &error, file.string().c_str(), baseDir.string().c_str());
    if (!ret) {
        std::cerr << "Failed to load mesh " << file << std::endl;
        throw std::exception();
    }

    std::vector<Mesh> out;
    for (const auto& shape : inShapes) {
        assert(shape.mesh.indices.size() % 3 == 0);

        size_t startTriangle = 0;
        auto prevMaterialID = shape.mesh.material_ids[0];
        for (size_t endTriangle = 0; endTriangle < shape.mesh.indices.size() / 3; ++endTriangle) {
            // tinyobjloader does not automatically split the mesh into smaller sub meshes according to material so we have to do it ourselves.
            if (endTriangle == shape.mesh.indices.size() / 3 - 1)
                ++endTriangle; // End of the tinyobj.shape; write remaining mesh.
            else if (shape.mesh.material_ids[endTriangle] == prevMaterialID)
                continue;
            else
                prevMaterialID = shape.mesh.material_ids[endTriangle];

            Mesh mesh;
            // vertexCashe now uses type VertexKey, so we can uniquely identify vertices
            std::unordered_map<VertexKey, uint32_t, std::hash<VertexKey>> vertexCache;
            for (size_t i = startTriangle * 3; i != endTriangle * 3; i += 3) {
                const glm::vec3 v0 = construct_vec3(&inAttrib.vertices[3 * shape.mesh.indices[i + 0].vertex_index]);
                const glm::vec3 v1 = construct_vec3(&inAttrib.vertices[3 * shape.mesh.indices[i + 1].vertex_index]);
                const glm::vec3 v2 = construct_vec3(&inAttrib.vertices[3 * shape.mesh.indices[i + 2].vertex_index]);
                const auto geometricNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

                // Load the triangle indices and lazily create the vertices.
                glm::uvec3 triangle;
                // Loop over each vertex of the triangle (3 vertices per triangle).
                for (unsigned j = 0; j < 3; ++j) {
                    const auto& tinyObjIndex = shape.mesh.indices[i + j];
                    // Creates a VertexKey struct instance for the current vertex using its position, normal, and texture coordinate indices.
                    VertexKey key { tinyObjIndex.vertex_index, tinyObjIndex.normal_index, tinyObjIndex.texcoord_index };

                    Vertex vertex;
                    vertex.position = construct_vec3(&inAttrib.vertices[3 * tinyObjIndex.vertex_index]);
                    vertex.normal = (tinyObjIndex.normal_index != -1 && !inAttrib.normals.empty()) ? construct_vec3(&inAttrib.normals[3 * tinyObjIndex.normal_index]) : geometricNormal;
                    vertex.texCoord = (tinyObjIndex.texcoord_index != -1 && !inAttrib.texcoords.empty()) ? glm::vec2(inAttrib.texcoords[2 * tinyObjIndex.texcoord_index], inAttrib.texcoords[2 * tinyObjIndex.texcoord_index + 1]) : glm::vec2(0.0f);

                    // Look up in the cache
                    auto iter = vertexCache.find(key);
                    if (iter != vertexCache.end()) {
                        // If the vertex exists in the cache, it reuses the existing vertex index for the current triangle vertex.
                        triangle[j] = iter->second;
                    } else {
                        // If the vertex does not exist in the cache, it assigns a new index, adds the vertex to the mesh, and updates the cache.
                        uint32_t newIndex = static_cast<uint32_t>(mesh.vertices.size());
                        vertexCache[key] = newIndex;
                        triangle[j] = newIndex;
                        mesh.vertices.push_back(vertex);
                    }
                    // Like this, vertices with different texcoord will still be unique even if they have the same position
                }
                mesh.triangles.push_back(triangle);
            }

            const auto materialID = shape.mesh.material_ids[startTriangle];
            if (materialID == -1) {
                mesh.material.kd = glm::vec3(1.0f);
                mesh.material.ks = glm::vec3(0.0f);
                mesh.material.shininess = 1.0f;
            } else {
                const auto& objMaterial = inMaterials[materialID];
                mesh.material.kd = construct_vec3(objMaterial.diffuse);
                if (!objMaterial.diffuse_texname.empty()) {
                    mesh.material.kdTexture = std::make_shared<Image>(baseDir / objMaterial.diffuse_texname);
                }
                mesh.material.ks = construct_vec3(objMaterial.specular);
                mesh.material.shininess = objMaterial.shininess;
                mesh.material.transparency = objMaterial.dissolve;
            }

            out.push_back(std::move(mesh));

            startTriangle = endTriangle;
        }
    }

    if (centerAndNormalize)
        centerAndScaleToUnitMesh(out);

    return out;
}

static void centerAndScaleToUnitMesh(std::span<Mesh> meshes)
{
    std::vector<glm::vec3> positions;
    for (const auto& mesh : meshes)
        std::transform(std::begin(mesh.vertices), std::end(mesh.vertices),
            std::back_inserter(positions),
            [](const Vertex& v) { return v.position; });
    const glm::vec3 center = std::accumulate(std::begin(positions), std::end(positions), glm::vec3(0.0f)) / static_cast<float>(positions.size());
    float maxD = 0.0f;
    for (const glm::vec3& p : positions)
        maxD = std::max(glm::length(p - center), maxD);
    /*// REQUIRES A MODERN COMPILER
      const float maxD = std::transform_reduce(
              std::begin(vertices), std::end(vertices),
              0.0f,
              [](float lhs, float rhs) { return std::max(lhs, rhs); },
              [=](const Vertex& v) { return glm::length(v.pos - center); });*/

    for (auto& mesh : meshes) {
        std::transform(std::begin(mesh.vertices), std::end(mesh.vertices),
            std::begin(mesh.vertices), [=](Vertex v) {
                v.position = (v.position - center) / maxD;
                return v;
            });
    }
}

Mesh mergeMeshes(std::span<const Mesh> meshes)
{
    Mesh out;
    out.material = meshes[0].material;
    for (const auto& mesh : meshes) {
        const auto vertexOffset = out.vertices.size();
        out.vertices.resize(out.vertices.size() + mesh.vertices.size());
        std::copy(std::begin(mesh.vertices), std::end(mesh.vertices), std::begin(out.vertices) + vertexOffset);

        for (const auto& tri : mesh.triangles) {
            out.triangles.push_back(tri + (unsigned)vertexOffset);
        }
    }
    return out;
}

void  meshFlipX(Mesh& mesh)
{
    for (auto& v : mesh.vertices) {
        v.position.x = -v.position.x;
        v.normal.x = -v.normal.x;
    }
}

void  meshFlipY(Mesh& mesh)
{
    for (auto& v : mesh.vertices) {
        v.position.y = -v.position.y;
        v.normal.y = -v.normal.y;
    }
}

void meshFlipZ(Mesh& mesh)
{
    for (auto& v : mesh.vertices) {
        v.position.z = -v.position.z;
        v.normal.z = -v.normal.z;
    }
}