# Sailboat tech demo: plan (CS4515 final project)

This replaces the inline-4 engine concept (`project_summary.md` and `engine_mechanics.md`
are kept for reference only). Time budget: **36 hours from 2026-11-03**, including report
and video. What is built: [ARCHITECTURE.md](ARCHITECTURE.md). What is left and who does what:
[TODO.md](TODO.md).

## Concept

A small wooden sailboat races a buoy course on an endless ocean. One wind control
(direction + strength) drives everything: the waves, the sail and boom, the wind vane,
how far the boat heels, and (optionally) how fast it sails. A time-of-day slider moves
the sun and recolours the procedural sky that the water and the boat reflect.

## Minimal requirements

| Requirement | Implementation | Where |
|---|---|---|
| Multiple viewpoints | Chase camera, deck camera attached to the hull node, orthographic top view of the course, free orbit | `camera.*` |
| Advanced shading | Cook-Torrance (GGX, Smith-Schlick, Schlick Fresnel), metallic/roughness, Blinn-Phong as a toggleable comparison | `pbr_frag.glsl` |
| Material textures | Procedurally generated albedo / roughness / normal maps: varnished mahogany strakes + antifouling (hull), teak planks with caulking (deck) | `procedural_texture.*` |
| Normal mapping | Tangents computed per mesh (Lengyel) in `GPUMesh`, TBN in the PBR shader; scrolling detail normals on the water | `mesh.cpp`, `pbr_frag.glsl`, `water_frag.glsl` |
| Environment mapping | Sky rendered into a cubemap (with mips); skybox, Fresnel reflections on water, split-sum-style IBL on the boat | `sky.*`, `pbr_frag.glsl`, `water_frag.glsl` |
| Smooth paths | Closed course of 5 cubic Bézier segments (C1, built Catmull-Rom style), boat follows it; curve + control polygon toggleable | `bezier.*`, `line_renderer.*` |
| Hierarchical transformations | Boat → hull → {deck, keel, rudder → tiller, mast → {boom → sail, vane}}, all built and animated in code | `scene_node.*`, `boat.*` |

## Extras (cut from the bottom when time runs out)

| Tier | Feature | Status | Owner |
|---|---|---|---|
| 1 | Procedural Gerstner water in the vertex shader, endless grid that follows the camera | skeleton done | |
| 1 | Constant speed along the Bézier (arc-length table), toggle to compare with naive `u` | skeleton done | |
| 1 | Buoyancy: CPU evaluates the same Gerstner sum at 4 hull points → heave, pitch, roll (+ wind heel) | skeleton done | |
| 2 | Day-night: sun path, sky colour, moonlight, stars | basic version done (slider + auto cycle) | |
| 2 | Shadow map from the sun/moon: PCF, slope-scaled bias, texel snapping, edge fade, debug view | done (one light) | |
| 2 | Second shadowed light (lighthouse spotlight, perspective shadow map) for "shadows for multiple lights" | todo | |
| 2 | Bloom on the HDR target (sun glints, lighthouse lamp) | implemented (B1); scene screenshots pending | Track B |
| 3 | Spray / wake particles at the bow, drifting with the wind | todo | |
| 3 | Minimap: top camera into an FBO, shown with `ImGui::Image` | todo | |
| 3 | Sail billow as blend shapes (flat ↔ full) instead of the current scale | todo | |
| 3 | Island + lighthouse (procedural terrain, rotating lamp, spotlight) | todo | |
| 3 | Crest foam, flag with animated texture, free-steering mode | todo | |

The report's first page needs a percentage per member **per feature**: fill in the Owner column
as you claim things, and keep it honest.

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # Debug enables the GL debug callback
ninja -C build && ./build/Master_TechDemo
```

- Keys: `1`–`4` cameras, `C` curve, `Space` pause the boat, `P` screenshot (saved to `screenshots/`, scene only, no UI).
  In the orbit camera, left-drag rotates and the scroll wheel zooms.
- Reproducible screenshots without opening a window:
  `./build/Master_TechDemo --capture out.png --frames 400 --camera deck --time 17.6 --wind 330`
- `--set name=value` overrides any toggle (names as in `FeatureToggles`, list in `applySetting()`), so on/off pairs
  for the report are one command each: `--set shadows=0`, `--set pcfRadius=0`, `--set showShadowMap=1`, ...
- Do **not** hand in `build/` (−0.5). `.gitignore` already excludes it.

## Code layout

```
src/
  application.cpp        main loop, UI panel, input, owns everything
  feature_toggles.h      every UI flag and parameter in one struct
  camera.*               chase / deck / top / orbit cameras
  bezier.*               cubic Bézier, closed C1 path, arc-length table
  scene_node.*           world = parentWorld · rest · anim, collects draw items
  boat.*                 builds the boat meshes and node tree, animates it from wind + waves
  waves.*                Gerstner wave set (shared by CPU buoyancy and the water shader)
  water.*                grid mesh that is re-centred under the camera
  sky.*                  procedural sky → cubemap (environment map), skybox
  framebuffer.*          HDR render target (RGBA16F + depth)
  line_renderer.*        lines/points for the curve and control polygon
  procedural_mesh.*      box, cylinder, cone, sphere, hull loft, deck, sail
  procedural_texture.*   plank/strake/water-normal textures generated at startup
  renderer.*             all draw passes: sky → opaque PBR → water → lines → tonemap
  mesh.*, texture.*      starter code, extended with tangents and raw-data textures
shaders/  pbr, water, sky (cubemap faces), skybox, line, fullscreen, post (tonemap)
```

Rules that keep two people from colliding:
- The simulation side (`boat`, `bezier`, `camera`, `waves` CPU part) never calls OpenGL.
- Everything toggleable lives in `FeatureToggles`; the UI only edits that struct.
- New shaders: add the file to `shaders/` **and re-run CMake** (the copy step globs at configure time).
- New `.cpp`: add it to `add_executable` in `CMakeLists.txt`.

## Frame order

1. Update: dt → waves (smoothly follow wind) → boat along path + buoyancy → hierarchy world matrices → camera.
2. Sky cubemap is re-rendered only when the sun moves.
3. Render into the HDR target: skybox → boat + buoys (PBR) → water → Bézier lines.
4. Tonemap (ACES) + gamma to the screen, then ImGui.

## Schedule (36 h, two people)

| Hours | Work |
|---|---|
| 0–2 | Build the skeleton, agree on owners |
| 2–12 | Polish minimal features until each has a clear on/off screenshot pair |
| 12–26 | Tier 2 then tier 3 extras |
| 26–36 | Feature freeze. Screenshots (use the *Save screenshot* button), report, ≤5 min video, clean build check, submit early |

## Report notes to collect as you go

- Gerstner: why not plain sines (sharper crests), steepness normalisation `Q_i = Q / (k_i A_i N)` to avoid loops,
  wavelengths fixed and only amplitude/direction follow the wind (changing `k` would make the phase `ωt` jump).
- CPU height query: Gerstner moves points sideways, so `heightAt(x)` first inverts the horizontal displacement
  with a few fixed-point iterations.
- Bézier: Catmull-Rom → Bézier conversion gives C1 joins; arc-length table + binary search for constant speed.
- Sail: non-uniform scale on the sail node → normals need the inverse-transpose; sign flip = tack/jibe.
- IBL: cubemap mip level ≈ roughness, Karis' analytic EnvBRDF approximation instead of a LUT.
- Shadows (`shadow_map.*`, `lightVisibility()` in `pbr_frag.glsl` / `water_frag.glsl`):
  - Directional light → orthographic light box, fitted around the boat only (9 m half-size) instead of the whole
    ocean: 2048² over 18 m ≈ 9 mm per texel. The box lives in light space, so long evening shadows still fit.
  - Box centre snapped to whole texels → no shimmering edges while the boat moves.
  - Acne: `glPolygonOffset` in the depth pass + slope-scaled bias (∝ tan of the light angle) in the lookup.
  - PCF: hardware depth compare (`sampler2DShadow` + `GL_LINEAR` = bilinear 2×2 per tap) over a (2r+1)² grid.
  - Border = depth 1 (lit) and a fade near the box edge, so the area limit is never visible.
  - Only direct light is shadowed; the sky (IBL) still lights shadowed areas, so shadows are blue-ish, not black.
  - Water receives but does not cast (self-shadowing waves would need a much larger bias).
  - Screenshot pairs: `--set shadows=0` vs default, `--set pcfRadius=0` vs `3`, `--set showShadowMap=1`.

## References (start list)

- Tessendorf, *Simulating Ocean Water*, SIGGRAPH course notes, 2001.
- Finch, *Effective Water Simulation from Physical Models*, GPU Gems ch. 1, 2004.
- Karis, *Real Shading in Unreal Engine 4*, SIGGRAPH 2013 (PBR, split-sum IBL).
- Karis, *Physically Based Shading on Mobile*, 2014 (analytic EnvBRDF).
- Lengyel, *Computing Tangent Space Basis Vectors for an Arbitrary Mesh*, 2001.
- Narkowicz, *ACES Filmic Tone Mapping Curve*, 2016.
- Williams, *Casting Curved Shadows on Curved Surfaces*, SIGGRAPH 1978 (shadow mapping).
- Reeves, Salesin, Cook, *Rendering Antialiased Shadows with Depth Maps*, SIGGRAPH 1987 (PCF).
- Microsoft, *Common Techniques to Improve Shadow Depth Maps* (texel snapping, slope-scaled bias).
