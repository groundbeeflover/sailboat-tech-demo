# Sailboat Tech Demo: architecture and what is built

State as of 2026-11-03. This describes the code that exists now; the work still to do is in
[TODO.md](TODO.md), the original plan in [sailboat_plan.md](sailboat_plan.md).

A small wooden sailboat races a buoy course on an endless ocean. One wind control (direction and
strength) drives the waves, the sail, the boom, the wind vane, the heel and the boat speed. A
time-of-day slider moves the sun, recolours the procedural sky, and with it everything that
reflects the sky. Every feature can be switched on and off from the ImGui panel.

| Chase camera | Orbit camera, shadows + shadow-map view |
|---|---|
| ![Chase camera](images/chase.png) | ![Orbit camera with shadows](images/orbit_shadows.png) |
| **Deck camera (node in the hierarchy)** | **Top view: Bézier course + boat marker** |
| ![Deck camera](images/deck.png) | ![Top view](images/top.png) |

---

## 1. Running it

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # Debug enables the OpenGL debug callback
ninja -C build && ./build/Master_TechDemo
```

| Input | Action |
|---|---|
| `1` `2` `3` `4` | Chase / deck / top / orbit camera |
| left drag, scroll | Rotate / zoom (orbit camera only) |
| `C` | Toggle the Bézier curve |
| `Space` | Pause the boat |
| `P` | Screenshot of the scene (without UI) to `screenshots/shot_<time>.png` |

Reproducible screenshots without a window (fixed 1/60 s time step):

```bash
./build/Master_TechDemo --capture out.png --frames 400 --camera deck --time 17.6 --wind 330 --set shadows=0
```

`--set name=value` accepts every field listed in `applySetting()` in
[application.cpp](../src/application.cpp) (names are the `FeatureToggles` field names).

---

## 2. Feature coverage

### Minimal requirements

| Requirement | What we did | Main code | UI toggle |
|---|---|---|---|
| Multiple viewpoints | Chase, deck (camera is a node in the boat hierarchy), orthographic top view with boat marker, free orbit | [camera.cpp](../src/camera.cpp) | Viewpoint → Camera, keys 1–4 |
| Advanced shading | Cook-Torrance: GGX distribution, Smith-Schlick geometry, Schlick Fresnel, metallic/roughness; Blinn-Phong for comparison | [pbr_frag.glsl](../shaders/pbr_frag.glsl) | Shading → PBR |
| Material textures | Generated albedo, roughness and normal maps: lapstrake mahogany + antifouling hull, teak deck | [procedural_texture.cpp](../src/procedural_texture.cpp) | Shading → Material textures |
| Normal mapping | Per-vertex tangents (Lengyel) in an extra VBO, TBN in the shader; scrolling ripple normals on the water | [mesh.cpp](../src/mesh.cpp), [pbr_frag.glsl](../shaders/pbr_frag.glsl), [water_frag.glsl](../shaders/water_frag.glsl) | Shading → Normal mapping, Water → Detail normal map |
| Environment mapping | Procedural sky rendered into a cubemap; skybox, Fresnel reflections on the water, image-based light on the boat | [sky.cpp](../src/sky.cpp), [sky_frag.glsl](../shaders/sky_frag.glsl) | Shading → Environment map / Skybox |
| Smooth paths (≥ 3 cubic Béziers) | Closed C1 course of 5 cubic segments; curve and control polygon drawn optionally | [bezier.cpp](../src/bezier.cpp), [course.cpp](../src/course.cpp) | Boat and course → Show Bezier curve / control polygon |
| Hierarchical transformations | Boat → hull → mast → boom → sail, rudder → tiller, wind vane, deck camera; built and animated in code | [boat.cpp](../src/boat.cpp), [scene_node.cpp](../src/scene_node.cpp) | (always on; wind/pause change it) |

### Extras done

| Extra | What we did | UI toggle |
|---|---|---|
| Constant speed along a Bézier | Arc-length table + binary search; can switch to naive parameter speed to compare | Boat and course → Constant speed |
| Procedural water | Sum of up to 8 Gerstner waves in the vertex shader on an endless camera-following grid | Water → Gerstner waves, Wave count, Wave height, Wireframe |
| Buoyancy (own feature) | The CPU evaluates the same wave sum at 4 hull points → heave, pitch, roll; buoys bob too | Boat and course → Buoyancy |
| Wind-driven rig (own feature) | Boom trims to the wind with a rate limit (visible tacks/gybes), sail billows to leeward, heel, vane, speed polar | Wind → From, Strength; Boat → Wind affects speed |
| Changing lighting (day-night) | Sun path, sunset colours, moonlight, stars; sky cubemap re-rendered when the sun moves | Sky and light → Time of day, Day-night cycle |
| Shadows (one light) | Sun/moon shadow map, PCF, slope-scaled bias, texel snapping, edge fade, debug view | Shadows → … |
| HDR + tone mapping | RGBA16F scene target, ACES filmic curve, exposure (prepares bloom) | Sky and light → Exposure |

---

## 3. Module map

The simulation side never calls OpenGL in its update code; the renderer never knows it is drawing a boat.
They meet in plain data: `RenderLists` (`DrawItem`s, `LineBatch`es, extra passes), `CameraView`,
`Lighting`, `SpotLight` and `FrameContext`. Section 7.12 lists the extension points for new features.

```mermaid
flowchart LR
    subgraph App["application.cpp"]
        Application
        UI["drawUi() ImGui panel"]
        CLI["parseArguments() / applySetting()"]
    end

    Toggles["FeatureToggles<br/>(feature_toggles.h)"]

    subgraph Sim["Simulation and scene (CPU, no GL in update)"]
        Course["Course<br/>course.*"]
        Bezier["BezierPath / CubicBezier<br/>bezier.*"]
        Boat["Boat<br/>boat.*"]
        Scene["SceneNode<br/>scene_node.*"]
        Waves["Waves<br/>waves.*"]
        Cameras["Cameras<br/>camera.*"]
        Light["computeLighting()<br/>sky.*"]
    end

    subgraph Assets["Generated assets (startup)"]
        PMesh["procedural_mesh.*"]
        PTex["procedural_texture.*"]
    end

    subgraph GPU["Rendering (OpenGL 4.1)"]
        Renderer["Renderer<br/>renderer.*"]
        Sky["Sky cubemap<br/>sky.*"]
        Water["Water grid<br/>water.*"]
        Hdr["HdrTarget<br/>framebuffer.*"]
        Shadow["ShadowMap<br/>shadow_map.*"]
        GPUMesh["GPUMesh / Texture<br/>mesh.*, texture.*"]
        Shaders["shaders/*.glsl"]
    end

    UI --> Toggles
    CLI --> Toggles
    Toggles -.read by.-> Sim
    Toggles -.read by.-> Renderer
    Application --> Course & Boat & Waves & Cameras & Light & Sky & Water & Renderer
    Course --> Bezier
    Course --> Scene
    Boat --> Scene
    Boat -.samples.-> Waves
    Course -.samples.-> Waves
    Boat --> PMesh & PTex
    Water --> PTex
    PMesh --> GPUMesh
    PTex --> GPUMesh
    Renderer --> Hdr & Shadow & Shaders
    Renderer -.uses.-> Sky & Water & GPUMesh
```

### Files

| File | Responsibility |
|---|---|
| `application.cpp` | `Application` (owns everything, main loop, UI panel, input, screenshots), command line, `main` |
| `feature_toggles.h` | One struct with every UI flag and parameter |
| `camera.*` | `Cameras`: chase / deck / top / orbit → `CameraView` |
| `bezier.*` | `CubicBezier`, `BezierPath` (closed C1 loop, arc-length table) |
| `course.*` | Anchor points, the path, buoys, debug lines for curve and control polygon |
| `boat.*` | Boat meshes, materials, node tree, per-frame motion and animation |
| `scene_node.*` | Transformation hierarchy: `world = parent.world · rest · anim` |
| `waves.*` | Gerstner wave set, CPU displacement/normal/height query |
| `water.*` | Camera-following grid mesh + ripple normal texture |
| `sky.*` | `computeLighting(hours)`, procedural sky → cubemap |
| `renderer.*` | All passes: shadow → opaque → water → lines → skybox → tonemap → shadow-map view |
| `framebuffer.*` | `HdrTarget` (RGBA16F colour + depth) |
| `shadow_map.*` | `ShadowMap` (depth texture with compare mode), `directionalLightMatrix()` |
| `procedural_mesh.*` | Hull, deck, sail, box, cylinder, sphere, helpers |
| `procedural_texture.*` | Plank/strake/ripple textures (pure CPU) |
| `mesh.*`, `texture.*` | Starter code, extended: tangent VBO, raw-pixel textures, sRGB, fixed move assignment |
| `uniforms.h` | Silent uniform setters for the bound program |
| `draw_item.h` | `PBRMaterial`, `DrawItem`, `LineBatch`, `ExtraPass`, `RenderLists`: what the scene hands to the renderer |
| `lights.h` | `SpotLight`: second light, filled by the scene, read by the renderer |

---

## 4. Class diagrams (UML)

### 4.1 Simulation and scene

```mermaid
classDiagram
    direction LR

    class Application {
        -Window m_window
        -FeatureToggles m_toggles
        -Renderer m_renderer
        -Sky m_sky
        -Water m_water
        -Waves m_waves
        -Course m_course
        -Boat m_boat
        -Cameras m_cameras
        -Lighting m_lighting
        -float m_time
        +run() void
        -update(dt) void
        -drawUi() void
        -onKeyPressed(key) void
        -saveScreenshot(path) void
    }

    class FeatureToggles {
        +CameraMode camera
        +float windFromDeg
        +float windStrength
        +bool constantSpeed
        +bool buoyancy
        +bool waves
        +bool pbr
        +bool shadows
        +float timeOfDay
        +... ~40 fields
    }

    class Boat {
        -HullShape m_shape
        -vector~unique_ptr~GPUMesh~~ m_meshes
        -vector~unique_ptr~Texture~~ m_textures
        -PBRMaterial m_hullMaterial ...
        -unique_ptr~SceneNode~ m_root
        -SceneNode* m_rudder, m_boom, m_sail, m_vane, m_deckCamera
        -float m_u, m_distance, m_yaw
        -float m_heave, m_pitch, m_roll, m_heel
        -float m_boomAngle, m_billow, m_rudderAngle
        +update(dt, time, toggles, course, waves) void
        +root() SceneNode&
        +coursePosition() vec3
        +yaw() float
        +deckCameraWorld() mat4
        +telemetry() Telemetry
    }

    class Course {
        -vector~vec3~ m_anchors
        -BezierPath m_path
        -unique_ptr~SceneNode~ m_root
        -vector~SceneNode*~ m_buoys
        +setTension(t) void
        +update(time, waves) void
        +path() BezierPath
        +appendLines(out, toggles, waves, time) void
        +center() vec3
        +radius() float
    }

    class BezierPath {
        -vector~CubicBezier~ m_segments
        -vector~float~ m_tableU
        -vector~float~ m_tableDistance
        +closedLoop(anchors, tension)$ BezierPath
        +evaluate(u) vec3
        +tangent(u) vec3
        +length() float
        +distanceToParameter(s) float
        +parameterToDistance(u) float
        +sample(n) vector~vec3~
    }

    class CubicBezier {
        +vec3 p0, p1, p2, p3
        +evaluate(t) vec3
        +derivative(t) vec3
    }

    class Waves {
        -array~GerstnerWave,8~ m_waves
        -int m_count
        -float m_angle, m_strength
        +update(dt, windFrom, strength, count, scale, enabled) void
        +displacement(restXZ, t) vec3
        +normal(restXZ, t) vec3
        +heightAt(worldXZ, t) float
        +mainDirection() vec2
    }

    class GerstnerWave {
        +vec2 direction
        +float amplitude
        +float wavenumber
        +float omega
        +float phase
        +float steepness
    }

    class SceneNode {
        +string name
        +GPUMesh* mesh
        +PBRMaterial* material
        +mat4 rest
        +mat4 anim
        +mat4 world
        +float blendWeight
        +bool visible
        +vector~unique_ptr~SceneNode~~ children
        +addChild(name, mesh, material, rest) SceneNode&
        +updateWorld(parentWorld) void
        +collect(out) void
        +find(name) SceneNode*
    }

    class Cameras {
        -vec3 m_chasePosition
        -float m_orbitYaw, m_orbitPitch, m_orbitDistance
        +update(mode, inputs) CameraView
        +onMouseMove(cursor, dragging) void
        +onScroll(delta) void
    }

    class CameraInputs {
        +vec3 boatPosition
        +float boatYaw
        +mat4 deckCameraWorld
        +vec3 courseCenter
        +float courseRadius
        +float aspect
        +float dt
    }

    class CameraView {
        +mat4 view
        +mat4 projection
        +vec3 position
        +vec3 forward
        +bool orthographic
    }

    class HullShape {
        +float length, beam, draft, freeboard
        +halfBeam(u) float
        +depth(u) float
        +sheer(u) float
    }

    Application *-- FeatureToggles
    Application *-- Boat
    Application *-- Course
    Application *-- Waves
    Application *-- Cameras
    Course *-- BezierPath
    BezierPath *-- "5" CubicBezier
    Course *-- SceneNode : buoys
    Boat *-- SceneNode : boat tree
    Boat *-- HullShape
    SceneNode *-- SceneNode : children
    Waves *-- "0..8" GerstnerWave
    Boat ..> BezierPath : follows
    Boat ..> Waves : heightAt()
    Course ..> Waves : heightAt(), normal()
    Cameras ..> CameraInputs
    Cameras ..> CameraView : produces
```

### 4.2 Rendering

```mermaid
classDiagram
    direction LR

    class Renderer {
        -Shader m_pbrShader, m_waterShader, m_skyboxShader
        -Shader m_lineShader, m_postShader
        -Shader m_shadowShader, m_depthDebugShader
        -HdrTarget m_hdr
        -ShadowMap m_shadowMap
        -mat4 m_lightSpace
        +render(frame, lists, water, waves, sky, toggles) void
        -drawShadowMap(frame, items, toggles) void
        -setFrameUniforms(frame, sky, toggles) void
        -drawOpaque(frame, items, sky, toggles) void
        -drawWater(frame, water, waves, sky, toggles) void
        -drawLines(frame, lines) void
        -drawTransparent(frame, passes) void
        -drawSkybox(frame, sky) void
        -tonemap(frame, toggles) void
        -drawShadowMapDebugView(frame) void
    }

    class FrameContext {
        +CameraView camera
        +Lighting lighting
        +float time
        +ivec2 framebufferSize
        +vec3 shadowCenter
        +SpotLight spot
    }

    class SpotLight {
        +bool enabled
        +vec3 position
        +vec3 direction
        +vec3 color
        +float innerAngle, outerAngle
        +float range
    }

    class RenderLists {
        +vector~DrawItem~ opaque
        +vector~LineBatch~ lines
        +vector~ExtraPass~ transparent
    }

    class Lighting {
        +vec3 sunDirection
        +vec3 lightDirection
        +vec3 lightColor
        +vec3 ambientColor
    }

    class Sky {
        -Shader m_shader
        -GLuint m_cubemap, m_fbo
        -int m_faceSize
        -vec3 m_lastSun
        +update(sunDirection) void
        +cubemap() GLuint
        +maxLod() float
    }

    class Water {
        -GLuint m_vao, m_vbo, m_ibo
        -float m_snapStep
        -unique_ptr~Texture~ m_detailNormals
        +gridOffset(cameraPosition) vec2
        +draw() void
        +detailNormals() Texture&
    }

    class HdrTarget {
        -GLuint m_fbo, m_color, m_depth
        +resize(size) void
        +bind() void
        +colorTexture() GLuint
    }

    class ShadowMap {
        -GLuint m_fbo, m_depth
        -int m_resolution
        +resize(resolution) void
        +bind() void
        +depthTexture() GLuint
    }

    class DrawItem {
        +GPUMesh* mesh
        +PBRMaterial* material
        +mat4 model
        +float blendWeight
    }

    class PBRMaterial {
        +vec3 albedo
        +float roughness
        +float metallic
        +vec3 emissive
        +Texture* albedoMap
        +Texture* normalMap
        +Texture* roughnessMap
        +vec2 uvScale
        +vec2 uvOffset
        +bool doubleSided
    }

    class LineBatch {
        +vector~vec3~ points
        +LineMode mode
        +vec3 color
        +float pointSize
    }

    class GPUMesh {
        -GLuint m_vao, m_vbo, m_vboTangents, m_ibo
        +draw() void
    }

    class Texture {
        -GLuint m_texture
        +Texture(width, height, rgba, srgb)
        +bind(slot) void
    }

    Renderer *-- HdrTarget
    Renderer *-- ShadowMap
    Renderer ..> FrameContext
    Renderer ..> RenderLists
    RenderLists *-- DrawItem
    RenderLists *-- LineBatch
    FrameContext *-- SpotLight
    Renderer ..> Sky : cubemap
    Renderer ..> Water : grid + ripples
    FrameContext *-- Lighting
    FrameContext *-- CameraView
    DrawItem --> GPUMesh
    DrawItem --> PBRMaterial
    PBRMaterial --> Texture
    Water *-- Texture
```

---

## 5. The boat hierarchy

`world = parent.world · rest · anim`. **rest** is set once in the `Boat` constructor (layout),
**anim** is rewritten every frame in `Boat::update` (motion). Boat frame: +z forward (bow),
+y up, +x to port, origin on the waterline amidships, metres.

```mermaid
flowchart TD
    Boat["Boat (no mesh)<br/>anim = T(x, heave, z) · Ry(yaw) · Rx(pitch) · Rz(roll + heel)"]
    Hull["Hull: makeHull()<br/>rest = I"]
    Deck["Deck: makeDeck()"]
    Cabin["Cabin: box<br/>rest = T(0, y, -0.675)"]
    Rigging["Rigging: 4 thin cylinders<br/>(forestay, backstay, shrouds)"]
    Rudder["Rudder: blade + stock<br/>rest = T(0, 0.35, transom)<br/>anim = Ry(rudderAngle)"]
    Tiller["Tiller<br/>rest = T(0, 0.5, 0)"]
    Mast["Mast: tapered cylinder 8.6 m<br/>rest = T(0, deck, 0.8)"]
    Boom["Boom: cylinder 3.4 m aft<br/>rest = T(0, 1.15, 0)<br/>anim = Ry(boomAngle)"]
    Sail["Sail: makeSail()<br/>rest = T(0, 0.05, -0.08)<br/>anim = S(billow, 1, 1)"]
    Vane["WindVane<br/>rest = T(0, 8.6, 0)<br/>anim = Ry(atan2(wind.x, wind.z))"]
    Cam["DeckCamera (no mesh)<br/>rest = T(0.5, deck + 2, -3.3) · Ry(π) · Rx(-7°)"]

    Boat --> Hull
    Hull --> Deck & Cabin & Rigging & Rudder & Mast & Cam
    Rudder --> Tiller
    Mast --> Boom & Vane
    Boom --> Sail
```

Why the parts move the way they do (all in [boat.cpp](../src/boat.cpp) `Boat::update`):

| Node | Driven by | Rule |
|---|---|---|
| Boat | course + waves + wind | Position `B(u)` on the path; yaw `atan2(B'.x, B'.z)`; heave/pitch/roll from 4 wave samples, smoothed (τ ≈ 0.3 s); heel = 22° · strength · (side component of the wind) |
| Rudder (→ tiller) | turn rate | `-3 · yawRate`, clamped to ±0.6 rad, smoothed; the tiller swings opposite to the blade because it is rigidly attached on the other side of the pivot |
| Boom (→ sail) | apparent wind angle α | target `side · clamp(0.55α − 0.1, 0.05, 1.45)`, moved at most 1.2 rad/s, so tacks and gybes show the boom crossing |
| Sail | wind strength, boom side | non-uniform scale in boom-local x: sign = leeward side, size = fullness; never below 0.05 (singular normal matrix) |
| WindVane | wind in the boat frame | points into the wind |
| DeckCamera | (inherits everything) | view matrix = `inverse(world)` |

The sail scale is the reason the PBR vertex shader gets a real inverse-transpose normal matrix.

---

## 6. Interaction diagrams

### 6.1 Startup

Members are constructed in declaration order; the `Window` must come first because it creates the
OpenGL context everything else uploads to.

```mermaid
sequenceDiagram
    participant main
    participant App as Application
    participant Win as Window
    participant R as Renderer
    participant Sky
    participant Water
    participant Course
    participant Boat
    participant PT as procedural_texture
    participant PM as procedural_mesh

    main->>main: parseArguments(argc, argv)
    main->>App: Application(options)
    App->>Win: create window + GL 4.1 context + ImGui
    App->>R: compile 7 shader programs, ShadowMap.resize(2048)
    App->>Sky: compile sky shader, allocate 6 x 512² RGBA16F cubemap
    App->>Water: build 400 x 400 grid
    Water->>PT: generateWaterNormals(256)
    App->>Course: anchors, closedLoop(anchors, 1.0), buoy mesh + 5 nodes
    App->>Boat: Boat()
    Boat->>PT: generateHullStrakes(), generateDeckPlanks()
    Boat->>PM: makeHull, makeDeck, makeSail, boxes, cylinders
    Boat->>Boat: build SceneNode tree (rest matrices)
    App->>App: register key / mouse / scroll callbacks, apply --set options
    main->>App: run()
```

### 6.2 One frame

```mermaid
sequenceDiagram
    participant App as Application
    participant Win as Window
    participant Sky
    participant Waves
    participant Course
    participant Boat
    participant Cams as Cameras
    participant R as Renderer

    App->>Win: updateInput() (events, ImGui NewFrame)
    App->>App: dt = clamp(now - previous, 0.1) (fixed 1/60 when capturing)
    App->>App: drawUi() writes FeatureToggles
    rect rgb(235, 245, 255)
    note over App,Boat: update(dt)
    App->>App: timeOfDay += 24 dt / dayLength (if cycling)
    App->>App: lighting = computeLighting(timeOfDay)
    App->>Sky: update(sunDirection) (re-renders cubemap only if the sun moved)
    App->>Waves: update(dt, wind, strength, count, scale, enabled)
    App->>Course: setTension(), update(time, waves) (buoys bob)
    App->>Boat: update(dt, time, toggles, path, waves)
    end
    App->>Cams: update(mode, CameraInputs from boat + course)
    Cams-->>App: CameraView
    App->>Boat: root().collect(lists.opaque)
    App->>Course: root().collect(lists.opaque), appendLines(lists.lines)
    note over App: Track A slot: more collect() calls, lists.transparent passes, frame.spot
    App->>R: render(FrameContext, RenderLists, water, waves, sky, toggles)
    opt screenshot requested or capture finished
        App->>App: saveScreenshot() (glReadPixels before ImGui)
    end
    App->>Win: swapBuffers() (draws ImGui, then swaps)
```

### 6.3 Inside `Boat::update`

```mermaid
sequenceDiagram
    participant Boat
    participant Path as BezierPath
    participant Waves
    participant Tree as SceneNode tree

    alt constant speed
        Boat->>Boat: distance += speed · dt
        Boat->>Path: distanceToParameter(distance)
        Path-->>Boat: u (binary search in arc-length table)
    else naive
        Boat->>Boat: u += speed · dt / (length / segments)
        Boat->>Path: parameterToDistance(u)
    end
    Boat->>Path: evaluate(u), tangent(u)
    Boat->>Boat: yaw, yawRate, measured speed
    Boat->>Boat: wind in boat frame → wind angle α, side
    opt buoyancy on
        loop bow, stern, port, starboard
            Boat->>Waves: heightAt(point, time)
        end
        Boat->>Boat: heave, pitch, roll targets
    end
    Boat->>Boat: smooth heave/pitch/roll/heel, boom (rate-limited), billow, rudder
    Boat->>Tree: write anim of Boat, Rudder, Boom, Sail, WindVane
    Boat->>Tree: root.updateWorld()
```

### 6.4 Inside `Renderer::render`

```mermaid
sequenceDiagram
    participant R as Renderer
    participant SM as ShadowMap
    participant HDR as HdrTarget
    participant GPU as Default framebuffer

    opt toggles.shadows
        R->>R: lightSpace = directionalLightMatrix(light, boat, 9 m, 30 m, res)
        R->>SM: bind, clear depth, polygon offset on
        loop every DrawItem (boat parts, buoys)
            R->>SM: shadow_vert/frag with mvp = lightSpace · model
        end
    end
    R->>HDR: resize(framebuffer), bind, clear
    R->>HDR: drawOpaque: pbr shader, per item: model, normal matrix, material, maps
    R->>HDR: drawWater: water shader, 8 wave uniforms, gridOffset, ripples, wireframe?
    R->>HDR: drawLines: curve, handles, points (dynamic VBO)
    opt lists.transparent not empty
        R->>HDR: drawTransparent: blending on, depth writes off, call each ExtraPass(frame)
    end
    opt skybox on and not orthographic
        R->>HDR: drawSkybox: fullscreen triangle at depth 1, LEQUAL
    end
    R->>GPU: tonemap: exposure → ACES → gamma
    opt show shadow map
        R->>GPU: drawShadowMapDebugView (compare mode off while sampling)
    end
```

### 6.5 Wind and waves: one source, two consumers

The wave set is computed once per frame on the CPU and used twice: uploaded to the GPU to draw the
water, and evaluated on the CPU so the boat and buoys float on exactly the drawn surface.

```mermaid
flowchart LR
    Slider["Wind slider<br/>windFromDeg, windStrength"] --> WU["Waves::update<br/>eases direction 25°/s, strength 0.25/s"]
    WU --> Set["8 GerstnerWave<br/>dir, A, k, ω, φ, Q"]
    Set -->|uniform arrays| VS["water_vert.glsl gerstner()<br/>displaces the grid"]
    Set -->|CPU| H["Waves::heightAt()<br/>fixed-point inverse of sideways motion"]
    H --> Buoy["Boat heave / pitch / roll<br/>buoys bob and tilt"]
    Slider --> Rig["Boat::update<br/>boom, sail, heel, vane, speed"]
    Set -->|mainDirection| Rip["water_frag.glsl<br/>ripple scroll direction"]
```

### 6.6 Camera modes

```mermaid
stateDiagram-v2
    [*] --> Chase
    Chase --> Deck : key 2
    Chase --> Top : key 3
    Chase --> Orbit : key 4
    Deck --> Chase : key 1
    Top --> Chase : key 1
    Orbit --> Chase : key 1
    Deck --> Top : key 3
    Top --> Orbit : key 4
    Orbit --> Deck : key 2
    note right of Deck : view = inverse(DeckCamera.world)
    note right of Top : orthographic, no fog, no skybox, boat marker
    note right of Orbit : mouse drag + scroll, ignored when ImGui wants the mouse
```

### 6.7 Shadow lookup (per fragment)

```mermaid
flowchart TD
    A["world position"] --> B["p = lightSpaceMatrix · pos → [0,1]"]
    B --> C{"p.z > 1 ?"}
    C -->|yes| L1["lit (1.0)"]
    C -->|no| D["bias = shadowBias · (1 + tan θ_light)"]
    D --> E["(2r+1)² taps of sampler2DShadow<br/>each = hardware 2x2 compare, bilinear"]
    E --> F["average = visibility"]
    F --> G["fade to 1 near the box edge"]
    G --> H["multiply direct light only<br/>(sky/IBL stays)"]
```

---

## 7. Subsystems in detail

### 7.1 Toggles, UI and command line

- `FeatureToggles` ([feature_toggles.h](../src/feature_toggles.h)) is the single source of truth. The UI
  (`Application::drawUi`) writes it; everything else reads it. Keys write it too.
- `applySetting()` maps names to fields for `--set`. When you add a toggle, add it there as well so it
  can be scripted for report screenshots.

### 7.2 Course and Bézier path

- Five anchors around the origin; a buoy 9 m inside each turn.
- `BezierPath::closedLoop`: segment *i* from `P_i` to `P_{i+1}` with inner control points
  `P_i + τ(P_{i+1} − P_{i−1})/6` and `P_{i+1} − τ(P_{i+2} − P_i)/6` (Catmull-Rom tangents). Both
  neighbours of an anchor share the same tangent → C1. τ is the "Curve tension" slider.
- Arc length: 200 samples per segment, cumulative polyline length. `distanceToParameter` =
  `lower_bound` + linear interpolation. Constant speed: advance distance; naive: advance `u`.
- Debug lines are draped on the waves (`heightAt + 0.4`) so they stay visible.

### 7.3 Waves (Gerstner)

- Rest point `p`, wave *i*: `θ = k (D·p) − ωt + φ`, displacement
  `(Q A Dx cos θ, A sin θ, Q A Dz cos θ)`, deep-water dispersion `ω = √(g k)`.
- Fixed wavelengths 33, 21, 13.5, 8.6, 5.7, 3.9, 2.7, 1.9 m, spread ±70° around the wind.
  Amplitude `0.017 · λ · strength · scale`.
- Steepness `Q_i = 0.65 / (k_i A_ref,i N)` with the full-wind amplitude → Σ Q k A ≤ 0.65 at scale 1
  (no looping crests); the "Wave height" slider up to 2 deliberately allows loops for a report figure.
- Only direction and amplitude follow the wind; changing `k` would make `ωt` jump.
- `heightAt(x)`: Gerstner moves points sideways, so iterate `rest = x − horizontal(rest)` 4 times.

### 7.4 Water grid

- 400 × 400 vertices, half-size 450 m. Spacing grows quadratically: ≈ 0.34 m at the centre, ≈ 4 m at
  the edge. Re-centred under the camera every frame, snapped to the finest spacing → no swimming.
- Fog (`1 − exp(−(d/260)²)`) to the sky's horizon colour hides the edge.
- Fragment shading: analytic Gerstner normal + two scrolling ripple-normal layers (whiteout blend,
  faded with distance) → Schlick Fresnel (F0 = 0.02) between a body colour and the sky reflection,
  plus a GGX sun glint in HDR.

### 7.5 Sky, environment map and lighting

- `computeLighting(hours)`: sun on a tilted arc (sunrise 6:00, sunset 18:00), warm colour near the
  horizon, moonlight (dim, blue, opposite direction) below it.
- `Sky::update` renders `sky_frag.glsl` into the 6 faces of a 512² RGBA16F cubemap (face basis vectors
  follow the GL spec cubemap table) and builds mipmaps — only when the sun moved.
- The mips double as a cheap blur: the PBR shader picks `lod = roughness · (maxLod − 1)` for
  reflections and the blurriest mip as irradiance (split-sum idea, Karis' analytic EnvBRDF instead
  of a LUT). Seamless cube filtering is enabled.

### 7.6 Procedural geometry ([procedural_mesh.cpp](../src/procedural_mesh.cpp))

- **Hull**: 48 stations along the length; each section is a superellipse quadrant (n = 2.4) from keel
  to waterline plus a flared topside to the deck edge. Separate port/starboard grids (crisp keel,
  consistent tangent handedness); the port side has flipped winding. Transom = strips per section
  level. UV u = along the length, v = girth: 0 keel, 0.5 waterline, 1 deck edge — the texture uses
  that split for antifouling vs varnish.
- **Deck**: grid across the beam with camber, UVs in metres (planks along the boat).
- **Sail**: triangle grid, camber profile deepest at ~40 % chord, bulging to +x; the node scale flips it.
- Primitives: box (UVs in metres per face), cylinder/cone with caps, cylinder between two points,
  UV sphere. `computeSmoothNormals` = area-weighted face normals.

### 7.7 Procedural textures ([procedural_texture.cpp](../src/procedural_texture.cpp))

- Tileable value noise / fBm (lattice wraps with the period, so every texture tiles).
- Deck 512²: 8 planks per metre, caulking seams, butt joints, grain, per-plank tint; roughness;
  height → normals.
- Hull 1024 × 512: lapstrake height profile (sawtooth per strake) → strong normal-map lines; red
  antifouling below v = 0.5, cream boot stripe, varnished mahogany above.
- Water ripples 256²: sum of sines with integer wave vectors (tile exactly) + fBm.
- Colour maps are uploaded as sRGB (`GL_SRGB8_ALPHA8`) so the shader receives linear values.

### 7.8 Renderer, targets and texture units

| Pass | Target | Shader(s) | Notes |
|---|---|---|---|
| Shadow | `ShadowMap` (depth 24, 2048²) | `shadow_vert/frag` (starter) | polygon offset 1.5/2.0, no culling |
| Opaque | `HdrTarget` RGBA16F | `pbr_vert/frag` | no back-face culling (sail) |
| Water | `HdrTarget` | `water_vert/frag` | optional `glPolygonMode(GL_LINE)` |
| Lines | `HdrTarget` | `line_vert/frag` | round points via `gl_PointCoord` |
| Extra transparent passes | `HdrTarget` | the feature's own | `RenderLists::transparent`; blending on, depth writes off |
| Skybox | `HdrTarget` | `skybox_vert/frag` | depth 1, `GL_LEQUAL`, skipped in top view |
| Tonemap | default framebuffer | `fullscreen_vert` + `post_frag` | exposure → ACES → gamma 2.2 |
| Shadow-map view | default framebuffer corner | `fullscreen_vert` + `depth_debug_frag` | compare mode off while drawing |
| Sky cubemap | cubemap faces (on change) | `fullscreen_vert` + `sky_frag` | in `Sky::update` |

| Unit | PBR shader | Water shader |
|---|---|---|
| 0 | albedo map | ripple normal map |
| 1 | normal map | – |
| 2 | roughness map | – |
| 3 | environment cubemap | environment cubemap |
| 4 | shadow map (`sampler2DShadow`) | shadow map |

Uniforms are set with `uniform::set(name, value)` ([uniforms.h](../src/uniforms.h)), which stays silent for
uniforms the driver optimised away.

### 7.9 PBR shader ([pbr_frag.glsl](../shaders/pbr_frag.glsl))

`(k_D · albedo/π + D·G·F / (4 N·V N·L)) · lightColor · N·L · visibility + IBL + emissive`, then fog.
D = GGX (α = roughness²), G = Smith with Schlick-GGX (k = (r+1)²/8), F = Schlick. IBL as in 7.5.
Normal mapping builds a Gram-Schmidt TBN from the interpolated tangent (w = handedness). Double-sided
materials flip N towards the viewer.

### 7.10 Shadows ([shadow_map.cpp](../src/shadow_map.cpp))

- Orthographic box in light space around the boat (half-size 9 m, ±30 m deep), centre snapped to
  whole texels (no shimmering). Light = sun by day, moon at night.
- Depth texture with `GL_COMPARE_REF_TO_TEXTURE` + `GL_LINEAR` → each lookup is a 2×2 PCF;
  the shader averages a (2r+1)² grid. Border = 1 (lit), fade near the edge.
- Bias: `glPolygonOffset` in the depth pass + slope-scaled bias in the lookup.
- Water receives but does not cast.

### 7.11 Cameras ([camera.cpp](../src/camera.cpp))

- Chase: 17 m behind, 5.5 m up, heading only, exponential smoothing (frame-rate independent).
- Deck: node in the hierarchy, `view = inverse(world)` → rocks with the boat.
- Top: `glm::ortho` around the course, 500 m up; the renderer disables fog and the skybox.
- Orbit: yaw/pitch/distance around the boat.
- Perspective: 55° vertical FOV, near 0.2 m, far 2000 m.

### 7.12 Extension points (how a new feature plugs in)

Added so the two of us can build features in parallel without editing each other's files.

| You want to… | Use | Where |
|---|---|---|
| Draw new opaque meshes (lit, shadowed) | Build a `SceneNode` tree, `collect()` it into `lists.opaque` | Track A slot in `Application::run` |
| Draw something blended (particles, beams) | `lists.transparent.push_back([this](const FrameContext& f) { … })` — your own shader/VAO; state contract in `renderer.h` | Track A slot in `Application::run` |
| Add a second light | Fill `frame.spot` (`SpotLight`, [lights.h](../src/lights.h)); the renderer reads it | Track A slot in `Application::run` |
| Deform a mesh with blend shapes | Upload `target − base` positions/normals as vertex attributes 4 and 5, set `SceneNode::blendWeight`. `pbr_vert.glsl` and `shadow_vert.glsl` already apply `blendWeight · delta`; meshes without those attributes read (0, 0, 0) | `mesh.cpp` (GPUMesh upload), your scene code |
| Animate a texture (flipbook) | Change `PBRMaterial::uvScale/uvOffset` each frame from the simulation time; the shader uses `uv · uvScale + uvOffset` | your scene code |
| Add a toggle | Field in `FeatureToggles` (your track's section), entry in `applySetting()` (your track's comment), UI method + one call in `drawUi()` (your track's slot) | `feature_toggles.h`, `application.cpp` |
| Add a source file | One line in your track's section of `add_executable` | `CMakeLists.txt` |

Every shared file has `// ---- Track A …` and `// ---- Track B …` comment slots, so your additions land
in different places and git merges them without conflicts.

---

## 8. Conventions and gotchas

- Units are metres, seconds, radians (degrees only in the UI). World: +y up. Wind angle = direction
  the wind comes **from**, 0 = from +z.
- Every animation uses real `dt` (clamped to 0.1 s), never frame counts.
- OpenGL 4.1 core (macOS limit): no compute shaders, no `layout(binding = …)`, no `#include` in GLSL.
  `fog`, `lightVisibility` are duplicated in `pbr_frag.glsl` and `water_frag.glsl` — keep them in sync.
  `gerstner()` in `water_vert.glsl` must match `Waves::displacement/normal`.
- New `.cpp` → add it to `add_executable` in `CMakeLists.txt`. New shader → re-run CMake (shaders are
  globbed at configure time).
- Don't commit `build/` (−0.5 at hand-in); `.gitignore` covers it.

## 9. Known limitations

- Only one shadowed light (the brief's extra asks for multiple).
- The sail's back side gets no direct light (no translucency).
- Stars are cubemap cells and look slightly square close up.
- The water does not self-shadow; no foam yet.
- Only tested on Linux (Mesa, AMD 780M) in a Debug build; check a Release build and the other
  teammate's OS early.
