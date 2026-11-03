#pragma once
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
