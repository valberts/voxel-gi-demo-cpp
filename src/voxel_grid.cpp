#include <algorithm> 
#include <vector> 

// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

class VoxelGrid {
public:
    int gridLength = 64; // size of voxel grid, cubic
    float worldLength = 2.0f; // actual world space size of world bounds
    float voxelScale = worldLength / gridLength; // size of voxels
    glm::vec3 worldMin = glm::vec3(-1.0f, -1.0f, -1.0f); // world bounds
    glm::vec3 worldMax = glm::vec3(1.0f, 1.0f, 1.0f);
    std::vector<glm::ivec3> occupiedPositions;

     /**
     * Checks if a given grid position is already occupied.
     *
     * @param gridPos The grid position to check, represented as a vec3i
     * @return True if the position is occupied, false otherwise.
     */
    bool isGridPositionOccupied(const glm::ivec3& gridPos) {
        return std::find(occupiedPositions.begin(), occupiedPositions.end(), gridPos) != occupiedPositions.end();
    }

    /**
    * Converts a world space position to a corresponding grid position.
    *
    * @param worldPos The world position to convert, represented as a vec3f
    * @return The corresponding grid position as a vec3i
    */
    glm::ivec3 worldToGridPosition(const glm::vec3& worldPos) {
        // normalize within [0,1]
        glm::vec3 normalizedPos = (worldPos - worldMin) / (worldMax - worldMin);

        // clamp to [0,1]
        normalizedPos = glm::clamp(normalizedPos, glm::vec3(0.0f), glm::vec3(1.0f));

        // convert world position to grid position
        return glm::ivec3(
            static_cast<int>(glm::floor(normalizedPos.x * gridLength)),
            static_cast<int>(glm::floor(normalizedPos.y * gridLength)),
            static_cast<int>(glm::floor(normalizedPos.z * gridLength))
        );
    }

    /**
    * Converts a grid position back to a world space position.
    *
    * @param gridPos The grid position to convert, represented as a vec3i
    * @return The corresponding world position as a vec3f
    */
    glm::vec3 gridToWorldPosition(const glm::ivec3& gridPos) {
        return worldMin + (glm::vec3(gridPos) + 0.5f) * voxelScale;
    }

    /**
     * Clears the grid by removing all occupied positions.
     */
    void clearGrid() {
        occupiedPositions.clear();
    }

    // Calculates the voxel scale based on world length and grid length
    void calculateVoxelScale() {
        voxelScale = worldLength / gridLength;
    }
};
