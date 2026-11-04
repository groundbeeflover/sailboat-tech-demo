#pragma once

// Every UI-controlled flag and parameter lives in this one struct. The UI panel is the only
// place that writes it; every other system just reads the values it cares about.

enum class CameraMode : int {
    Chase = 0, // behind the boat, smoothed
    Deck, // attached to a node in the boat hierarchy, rocks with the boat
    Top, // orthographic view of the whole course
    Orbit, // free camera orbiting the boat (left drag + scroll)
};

struct FeatureToggles {
    // Viewpoints
    CameraMode camera { CameraMode::Chase };

    // Wind (drives waves, sail, boom, vane, heel and boat speed)
    float windFromDeg { 330.0f }; // direction the wind blows FROM, 0 = from +z (330: beam reach on the first leg)
    float windStrength { 0.6f }; // 0..1

    // Boat and course
    bool pauseBoat { false };
    float boatSpeed { 6.0f }; // m/s at the best point of sail
    bool constantSpeed { true }; // arc-length parametrisation vs. naive Bezier parameter
    bool windAffectsSpeed { true };
    bool buoyancy { true };
    bool showCurve { true };
    bool showControlPolygon { false };
    float courseTension { 1.0f };

    // Water
    bool waves { true };
    int waveCount { 6 };
    float waveScale { 1.0f };
    bool waterDetailNormals { true };
    bool waterWireframe { false };

    // Shading
    bool pbr { true }; // Cook-Torrance, otherwise Blinn-Phong for comparison
    bool textures { true };
    bool normalMapping { true };
    bool environmentMap { true };
    bool skybox { true };
    bool fog { true };

    // Shadows (sun by day, moon at night)
    bool shadows { true };
    int shadowResolution { 2048 };
    int pcfRadius { 1 }; // percentage-closer filtering over (2r + 1)^2 lookups, 0 = hard edges
    float shadowBias { 0.0004f }; // in normalised light-space depth (1 = 2 * ShadowDepthRange metres)
    float shadowExtent { 9.0f }; // half-size of the light box around the boat (perpendicular to the light), metres
    bool showShadowMap { false };

    // Sky and lighting
    float timeOfDay { 15.5f }; // hours, 0..24
    bool dayNightCycle { false };
    float dayLengthSeconds { 120.0f };
    float exposure { 1.0f };

    // ---- Track A features (island, particles, sail blend shapes, flag, free sailing) ----
    bool island { true };
    float lighthouseSpeed { 0.6f }; // lamp rotation, rad/s (about 10 s per turn)

    // ---- Track B features (bloom, spotlight, minimap, foam) ----
};
