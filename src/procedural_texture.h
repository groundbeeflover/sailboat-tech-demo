#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <cstdint>
#include <vector>

// Texture images generated at startup, so the demo needs no downloaded assets.
// Pure CPU code: the caller uploads the pixels with Texture(width, height, pixels, srgb).

struct ImageRGBA {
    int width { 0 };
    int height { 0 };
    std::vector<uint8_t> pixels; // row 0 is texture coordinate v = 0
};

struct MaterialImages {
    ImageRGBA albedo; // sRGB encoded
    ImageRGBA normal; // tangent space, x = +u, y = +v
    ImageRGBA roughness; // linear, in the red channel
};

// Teak deck planks with dark caulking seams. One tile = 1 m x 1 m, planks run along v.
MaterialImages generateDeckPlanks(int size = 512);

// Hull sides. u runs along the whole hull, v is the girth (0 keel, 0.5 waterline, 1 deck edge):
// red antifouling below the waterline, a cream boot stripe, varnished mahogany strakes above.
MaterialImages generateHullStrakes(int width = 1024, int height = 512);

// Tileable small-scale ripple normals for the water surface.
ImageRGBA generateWaterNormals(int size = 256);

// A soft, lumpy puff for spray and foam particles. The shape is in alpha; RGB is white, the shader lights it.
ImageRGBA generateSprayPuff(int size = 64);


// A terrain height field sampled on a square grid: the input of generateTerrain().
struct TerrainSamples {
    int resolution { 0 }; // samples per side; sample (i, j) sits at u = i / (resolution - 1), v = j / (resolution - 1)
    float size { 0.0f }; // metres covered by one side of the grid
    std::vector<float> height; // metres above the waterline, index = j * resolution + i
    std::vector<glm::vec3> normal; // unit surface normals at the same samples
};

// The ground of an island seen from above, one texture over the whole grid (u along x, v along z):
// wet and dry sand near the waterline, grass on gentle slopes, layered rock on steep ones, with
// noise breaking up the borders. The normal map only adds small bumps; the mesh has the big shapes.
MaterialImages generateTerrain(const TerrainSamples& terrain, int size = 1024);
