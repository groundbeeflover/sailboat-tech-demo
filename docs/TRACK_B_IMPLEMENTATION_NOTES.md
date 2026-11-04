# Track B — renderer review, decisions and report notes

> Maintained as features are implemented. **B1 Bloom, B2 Lighthouse spotlight and B3 Minimap were accepted by the teammates; B4 Crest Foam is implemented on a separate branch awaiting integrated review.**
> A1+B1, and subsequently the B2 spotlight with its targeted shadow-caster fix, were confirmed
> working by the user on their workstation (2026-11-04). B3 was also verified and merged
> into V3 (A3+B3); B4 starts from that exact V3 `main` baseline.
>
> Do not copy the exact dates/owner percentages into the final report without confirming them
> against the two group members' actual contributions.

## 1. Assignment and codebase review

Read: `final_project.html`, root `README.md`, `docs/TODO.md`, `docs/ARCHITECTURE.md`,
`docs/sailboat_plan.md`, the project-local `src/` headers/implementations and shaders,
`CMakeLists.txt`, and the vendored library README files. The vendored libraries are dependencies,
not part of the student-written rendering features.

The assignment requires a **single** app with UI toggles for every feature; the report must explain
*how* and *why*, include **at least two screenshots per feature**, references and a first-page
per-member percentage breakdown. There is also a narrated video (maximum five minutes).

### Architecture at a glance

| Part | Code | How it matters to Track B |
|---|---|---|
| Main loop / toggles / capture | `src/application.cpp`, `src/feature_toggles.h` | UI and `--set name=value` must agree; reproducible screenshots use fixed `dt`. Add settings only in Track B slots. |
| Simulation | `boat`, `course`, `bezier`, `waves`, `camera`, `scene_node` | CPU wind/waves/course/hierarchy. Track A owns scene extensions; rendering consumes `FrameContext` and `RenderLists`. |
| Draw data | `draw_item.h`, `lights.h` | PBR materials, model matrices and blend weights; `FrameContext::spot` is Track A's B2 light hook. |
| Scene shaders | `shaders/pbr_*`, `water_*`, `sky_*`, `shadow_*` | PBR/IBL/normal maps, Gerstner waves, procedural HDR sky, sun shadow depth pass. |
| Renderer | `renderer.*`, `framebuffer.*`, `shadow_map.*` | Owns GL targets and draws all render passes; Track B owns it. |
| Post-process | `shaders/fullscreen_vert.glsl`, `post_frag.glsl` | Fullscreen triangle, linear scene HDR → exposure → ACES fit → display gamma. Bloom is inserted before tone mapping. |

Original frame order before B1: update simulation → optionally render sun/moon shadow map →
render opaque PBR, water, lines, extra transparent objects and skybox into `HdrTarget` (RGBA16F)
→ tone map to default framebuffer → optional shadow-map debug overlay → ImGui.

**Track A compatibility:** Track A adds meshes via `RenderLists::opaque`, particles/beam via
`RenderLists::transparent`, and eventually the lighthouse light via `FrameContext::spot`.
B1 uses the **already composited HDR image**, so it will automatically respond to emissive
lighthouse objects and transparent particle/glow passes without coupling to Track A classes.
No Track A slots were changed.

## 2. B1 — Bloom

**Status:** implemented and tested in isolation with a real OpenGL context. The user subsequently
confirmed A1+B1 run together correctly on their workstation (2026-11-04); report screenshots
and numerical performance measurements were not supplied.

### Aim and technique

Bright radiance such as the procedural sun disk, water's GGX glints, and later an emissive
lighthouse lantern should spill gently onto nearby pixels. Simply raising the exposure would
brighten everything and clip details, not create a localized halo. We therefore apply an
**HDR image-space bloom pass**.

```
Scene passes ------------------------------> HDR color RGBA16F -----------+
                                                      |                  |
                                             bright soft-knee             |
                                                  1/2 res                 |
                                                      |                  |
                                         Gaussian horizontal -> tex1      |
                                         Gaussian vertical   -> tex0      |
                                           repeat N times (1..6)         |
                                                      |                  |
                                                      +--------+---------+
                                                               |
                                               HDR += intensity * bloom
                                                               |
                                                exposure -> ACES -> gamma
                                                               |
                                                    default framebuffer
```

### Algorithm choices and reasoning

1. **Operate on linear HDR, not gamma-encoded display colour.** `HdrTarget` holds RGBA16F values
   above 1.0; the existing sky disk is roughly 20× radiance and water specular highlights can
   also be brighter than white. Extracting from an 8-bit tone-mapped image would lose which
   pixels were *actually* bright.
2. **Soft-knee thresholding** in `bloom_bright_frag.glsl`. Brightness is `max(R,G,B)` (a simple
   saturated-highlight-friendly test). A quadratic transition around `threshold` avoids hard
   bloom/no-bloom boundaries as waves or the camera move. The shader uses the formula documented
   in `docs/TODO.md`. The knee is `0.5 × threshold`. Threshold is measured **before exposure**
   so changing camera exposure does not change which source pixels are selected.
3. **Half-resolution RGBA16F buffers** in `BloomPass`. Downsampling cuts each expensive blur pass
   to a quarter of full-resolution pixels and gives a slightly wider footprint in screen pixels.
   A round-up on odd framebuffer sizes ensures coverage. Float format retains HDR intensity;
   bilinear filtering enables efficient paired taps. Two framebuffers are recreated only when
   the viewport size actually changes.
4. **Separable Gaussian blur**, horizontal then vertical, ping-ponging without reading and writing
   the same texture. A naive 9×9 2D kernel costs 81 fetches/pixel; two 1D 9-tap passes cost 18.
   Our shader takes advantage of bilinear texture filtering to combine adjacent weighted taps:
   **five samples per direction**. Weights per side:
   `w0=0.227027`, `(w1+w2)=0.3162162 at ±1.3846154 texels`,
   `(w3+w4)=0.0702703 at ±3.2307692 texels`. They sum to approximately 1, conserving
   brightness. `N` pairs (default 4) adjust halo width. Gaussian blur is easy to understand,
   easy to demonstrate and GL 4.1 compatible (no compute shader required).
5. **HDR additive composition before ACES** in `post_frag.glsl`:
   `hdr += bloomIntensity * blurredHighlights` then `hdr *= exposure` then the original
   Narkowicz ACES fit and gamma conversion. Bloom and the base scene share exposure/tonemapping;
   the original off path skips both bloom passes and texture sampling.
6. **Debug mode:** `showBloomBuffer` replaces scene HDR with the blurred bright layer *before*
   exposure/tone mapping. It shows the algorithm's intermediate output instead of merely
   indicating a checkbox has been activated. It is intentionally active only with `bloom=1`.
7. **Clamping and lifetime:** shader threshold is clamped to a positive minimum and the iteration
   loop to [1,6] for malformed CLI values. `BloomPass` deletes its textures and FBOs on
   destruction. No depth buffers are needed for fullscreen image filtering.

**Alternative considered:** a multi-scale downsample/upsample pyramid (Jorge Jimenez, 2014) produces
broader, softer, often more temporally stable glows at a low cost for large radii. For this
course demo, a fixed half-resolution Gaussian kernel makes the separability/optimization easy to
explain and the UI straightforward. Its trade-off is a fairly limited halo width even at six
iterations, plus repeated passes; replace it with a pyramid if profiling or visual tests show
that this is inadequate.

### Files changed, and why

| File | Change | Reason |
|---|---|---|
| `src/bloom.h` / `src/bloom.cpp` | New owned pass with bright extraction, two half-res textures/FBOs and repeated 2D blur passes | Separate GPU resource lifetime and algorithm from scene renderer; preserves Track A separation. |
| `shaders/bloom_bright_frag.glsl` | Bright-pixel extraction with soft knee | Avoid glowing the whole image and discontinuous hard thresholds. |
| `shaders/bloom_blur_frag.glsl` | 9-tap separable, paired-tap 5-fetch Gaussian | Lower bandwidth and consistent gaussian-shaped halo. |
| `src/renderer.h` / `.cpp` | Add `m_bloom`, render it after scene/sky, bind output on post sampler unit 1 | Preserve render-list contract, scene shader units 0–4 and existing shadow overlay order. |
| `shaders/post_frag.glsl` | Optional additive HDR composite and intermediate debug view | Bloom must be summed *before* exposure and ACES; disabled path leaves original image unchanged. |
| `src/feature_toggles.h` | Five B1 settings | UI and capture CLI use the same values. |
| `src/application.cpp` | CLI entries, `bloomUi()` and one call under the Track B slot | Changes confined to shared UI extension points. |
| `CMakeLists.txt` | Add `src/bloom.cpp` in Track B section | Compile source; CMake reconfiguration also discovers two new shaders. |
| `docs/TRACK_B_IMPLEMENTATION_NOTES.md` | Architecture, design, tests, report notes | Maintains rationale and evidence as requested. |

### UI and reproducible captures

After building on a machine with the required development packages, run the app and expand
**Bloom (HDR post-processing)**. Try the evening time (around 17.5–17.6 hours) and adjust the
camera until the sun or a strong reflection is visible. Compare on and off **from identical
camera/time/frame states**, then capture a bloom-buffer debug image:

```bash
./build/Master_TechDemo --capture screenshots/b1_bloom_off.png --frames 400 --camera deck --time 17.6 --set bloom=0
./build/Master_TechDemo --capture screenshots/b1_bloom_on.png --frames 400 --camera deck --time 17.6 --set bloom=1 --set bloomThreshold=1.5 --set bloomIntensity=0.6 --set bloomIterations=4
./build/Master_TechDemo --capture screenshots/b1_bloom_only.png --frames 400 --camera deck --time 17.6 --set bloom=1 --set showBloomBuffer=1
```

**IMPORTANT:** these are instructions, not images already captured. Inspect the output and
choose an appropriate view that shows the sun or water glitter clearly. The brief requires
*at least two genuine sailboat screenshots* for this feature. A capture without the sun/glints
may make bloom visually subtle. Optionally repeat at night after A1/B2 bring the lighthouse.

### Testing / validation record (2026-11-04)

- CMake configure of the **full application** was attempted on the current Linux sandbox, but
  stopped *before compilation* because the sandbox lacks GLFW's XRandR development header
  (`X11/extensions/Xrandr.h`). It also lacks the GLU development header (`GL/glu.h`).
  This does **not** indicate a project source error; it means an end-to-end executable build and
  course-scene screenshot were not possible here. The workstation needs its OS-specific OpenGL,
  GLFW/X11 and GLU development packages. Never package `build/` in the submitted ZIP.
- `g++ -std=c++20 -Wall -Wextra -fsyntax-only` **passed** for the three changed C++ translation
  units (`bloom.cpp`, `renderer.cpp`, `application.cpp`) using vendored project headers and
  an empty temporary GLU-header shim *outside the repository*. The single pre-existing warning
  is the defaulted `FrameContext::spot` aggregate initializer in `application.cpp`.
- A standalone **real GPU API test** linked the new `BloomPass` and shader builder against
  Mesa software OpenGL using surfaceless EGL (OpenGL 4.5 Core Profile, shaders are GLSL 4.10).
  The project-local bloom bright, blur and post shaders compiled and linked on the driver.
  Synthetic 64×64 HDR input had a 4×4 highlight at 8.0 radiance and 0.2 background:
  half-resolution red bloom samples were `center=0.338867`, `6 pixels away=0.0579224`,
  `corner=0.0`; the composite changed an adjacent test pixel from `0.577637` (off) to
  `0.725098` (on). Debug showed the extracted blur, and `glGetError()==GL_NO_ERROR`.
  The test harness was kept outside the repository since it is environment-specific.
- **Still to validate on the full scene:** build both Debug and Release; compare identical off/on
  screenshots; test resize, toggle switches, debug buffer, sky/water highlights, PCF debug overlay,
  Track A transparent particle pass and eventually the lighthouse emitter; inspect FPS cost.

### Draft B1 report paragraph (adapt after screenshots)

> We implemented bloom as an HDR post-processing effect to imitate the spread of light around
> bright sources, such as the sun, glints on the ocean and the lighthouse lamp. First, a
> soft-knee brightness threshold extracts highlights from the linear RGBA16F scene buffer at half
> resolution. A separable Gaussian filter blurs the result by alternating horizontal and vertical
> framebuffer passes. Bilinear sampling combines adjacent taps, reducing a nine-tap convolution
> to five texture fetches per direction. Finally, the blurred highlights are added to the scene
> **before exposure and ACES tone mapping**, preserving HDR intensities. We chose this method
> over a multi-resolution mip pyramid because it is compact, easy to parameterise and visually
> appropriate for the scene, although a pyramid would offer broader glow and potentially better
> performance for large radii. The UI exposes the threshold, intensity, iterations and a debug
> view so the effect can be evaluated independently.

### References for the final report

1. The course brief, `final_project.html` (the actual requirements), and our design guide
   `docs/TODO.md`, B1.
2. LearnOpenGL, *Bloom*: <https://learnopengl.com/Advanced-Lighting/Bloom> — two framebuffer
   ping-pong Gaussian blur, extract before final composition.
3. Jorge Jimenez, *Next Generation Post Processing in Call of Duty: Advanced Warfare*, SIGGRAPH
   Advances in Real-Time Rendering 2014: <https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/>
   — bloom pyramid alternative and temporal stability motivation. **We did not implement its
   full pyramidal technique**.
4. Krzysztof Narkowicz, *ACES Filmic Tone Mapping Curve* (2016):
   <https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/>
   — existing project's tone-map fit, now placed after HDR bloom composition.

## 3. B2 — Lighthouse spotlight and second shadow map

**Status (2026-11-04):** implemented, merged with A2, then corrected after the user found
lighthouse self-occlusion. The user tested the revised B2 on their workstation and confirmed that
spotlight lighting and shadows now work together. The isolated C++/OpenGL tests described below
pre-date the fix; their results should not be confused with full-scene validation.

### How it connects to A1 and B1

- Track A has already merged `frame.spot = m_island.lampLight()` into `Application::run()`.
  `Island::update()` rotates the lamp and fades its intensity up at night; `lampOn()` automatically
  disables it if the island is hidden. B2 **does not** modify `island.*` or `lights.h`.
- Track B samples `FrameContext::spot` through its existing public interface. When `spotlight=0`
  or the lamp is off, the new shading branch does nothing; when `spotShadows=0`, lighting is still
  present but no second depth pass runs and the contribution is unshadowed. The existing sun/moon
  `shadows` toggle only controls the orthographic map. The controls are intentionally independent
  to demonstrate **shadows for multiple light sources**.
- The colour is added to the **HDR scene**, before B1 extracts highlights, so bright lamp-lit
  surfaces can bloom without special coordination or altering the post-processing pipeline.

### Render sequence and algorithm

```text
if sun shadows: render sun/moon orthographic map (existing, texture unit 4)
if lamp active and spot shadows: render spotlight perspective map (new, unit 5)
HDR scene: PBR boat/island/terrain + water, evaluating sun + spotlight separately
B1 Bloom if enabled -> exposure/ACES tone mapping -> optional shadow-map debug overlays
```

The spotlight is **finite and directional**, unlike the sun. Its transform is
`P * V = glm::perspective(2 * outerAngle, 1, 0.5, range) *
glm::lookAt(position, position + direction, up)`, with a vertical-direction fallback up vector.
The angles stored in `SpotLight` are half-angles (default 8° inner, 12° outer).
The full cone field of view is twice the outer angle; a 1024² depth texture gives substantially
more resolution to the relevant cone than the existing sun's wide orthographic map. The near
plane of 0.5 m avoids extreme near-distance depth precision. Because the spot origin is
inside the solid lamp housing (which extends 0.65 m along the beam), the lamp mesh
must be excluded from its own depth pass or it will block the entire cone.

We reused `ShadowMap`'s depth texture and hardware `sampler2DShadow` comparison sampler rather
than inventing a second FBO class. The second map keeps its own FBO, projection, and binding
(texture unit **5**; sun shadow map remains on **4**; PBR material/IBL texture units remain 0–3).
The sun and spotlight passes reuse `renderDepthItems()`, including the `blendWeight` deformation
for moving sails. The water is a *receiver* but not a caster, avoiding a second wave-depth pass.
The spotlight depth pass renders the current opaque list except the emitting lamp mesh
(`SceneNode::castsSpotShadow = false` on the lamp); this avoids a self-occluding
shadow map. The island terrain, lantern structure and boat still occlude the rotating
lamp. The sun shadow pass can still receive the lamp mesh. Its GPU cost is one additional 1024² depth pass
when the lamp and spot shadows are active, plus spotlight BRDF calculations and PCF lookups in
PBR and water fragments. A more aggressive optimisation would cull meshes outside the cone.

### Spotlight BRDF, falloff, and shadow visibility

For a surface point `P`, `L = normalize(spot.position - P)`, `d = length(spot.position - P)`;
`cone = smoothstep(cos(outerAngle), cos(innerAngle), dot(-L, spot.direction))`. We pass both
cosines once per frame from the CPU, rather than calling `cos` in every fragment. Attenuation is
`cone * (1 - (d / range)^4)^2 / max(d², 1)` for `0 < d < range`, else zero. The inverse-square
term gives distance falloff, the soft inner-to-outer transition avoids a hard beam edge, and
the range factor avoids a sharp discontinuity at the far clip plane. The same formula is kept
in the PBR and water shaders (GLSL 4.1 has no built-in textual include system).

In `pbr_frag.glsl`, this HDR radiance passes through the *same* Cook–Torrance GGX/Schlick–Smith
BRDF already used for sunlight, with an equivalent Blinn–Phong branch when PBR is disabled.
Environment light, emission and the original sun remain independent: occluding the lamp cannot
black out a surface that still receives the sky. `water_frag.glsl` adds a GGX lamp glint and a
small diffuse scattering term to make the pool of light, and especially the boat's shadow on
the water, visible even when the narrow water specular highlight is not aligned with the camera.
These two shader paths intentionally do **not** render a separate volumetric cone mesh; that was
an optional extension in the TODO, not a core requirement.

For each fragment, `spotLightSpaceMatrix * vec4(P,1)` maps to light clip coordinates. We perform
perspective division, remap to `[0,1]`, and compare its `z` against the spotlight's depth texture
with hardware bilinear PCF; optional `(2r+1)^2` neighbourhood PCF uses the existing PCF-radius
slider. Samples outside the light frustum are unshadowed (the cone attenuation is already zero).
As with the sun, the bias grows with the light/normal slope, but **the bias parameter is separate**:
projected perspective depth is non-linear, so using the sun's orthographic `shadowBias=0.0004`
would be inappropriate. We start at `spotBias=0.00008` with a 0–0.0005 UI range; excessive bias
causes detached shadows ('peter-panning'), too little gives self-shadow acne. Polygon offset
(1.5, 2.0) remains enabled while rasterizing both depth maps. The shadow lookup uses the
**geometric** normal for the slope, not the normal-map perturbation.

An additional fullscreen debug shader converts perspective depth back to approximate linear
light-space distance before display, because raw spot depth otherwise appears almost uniformly
white. `showSpotShadowMap` draws this overlay beside the existing sun debug view; it can be
enabled independently via UI and `--set`.

### What changed

| File | B2 change and reason |
|---|---|
| `src/shadow_map.h/.cpp` | Add pure `spotLightMatrix()` computation; reuse `ShadowMap` rather than duplicate depth-FBO lifecycle. |
| `src/renderer.h/.cpp` | Own second depth target/transform, shared depth draw, conditional second pass, sampler unit 5, uniforms and debug overlay. |
| `shaders/pbr_frag.glsl` | Add shadowed spot radiance with physically motivated GGX and Blinn–Phong fallback. |
| `shaders/water_frag.glsl` | Add cone highlight plus a scattering footprint that can reveal the moving boat shadow. |
| `shaders/spot_depth_debug_frag.glsl` | Linearise the spotlight's non-linear depth for inspection and report figures. |
| `src/feature_toggles.h` | Independent enable, shadow enable, bias and debug flags under Track B. |
| `src/application.cpp` | Track B-only UI method/call and `--set` mappings; Track A's lamp hookup untouched. |
| `docs/TODO.md`, `docs/sailboat_plan.md`, this file | Implementation status, technical rationale, testing and report material. |

`CMakeLists.txt` needs **no code changes**: no new `.cpp` was added, and its existing shader glob
copies the new `.glsl` file after CMake is reconfigured.

### Reproducible validation and screenshots

Choose a viewpoint where the lighthouse beam crosses a visible section of water or the boat.
A1 rotates the lamp about `0.6 rad/s`, and the course is around 100 m from the island; if the
boat is outside the cone in a particular screenshot, adjust `--frames`, `--camera orbit` or the
interactive camera. Hold frame count and camera/time constant for each comparison pair:

```powershell
# Run on Windows from the project root after building the app.
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b2_light_off.png --frames 240 --camera orbit --time 22 --set spotlight=0 --set bloom=0
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b2_light_unshadowed.png --frames 240 --camera orbit --time 22 --set spotlight=1 --set spotShadows=0 --set bloom=0
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b2_light_shadowed.png --frames 240 --camera orbit --time 22 --set spotlight=1 --set spotShadows=1 --set bloom=0
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b2_shadow_debug.png --frames 240 --camera orbit --time 22 --set spotlight=1 --set spotShadows=1 --set showSpotShadowMap=1 --set bloom=0
```

The screenshot commands are examples, **not actual screenshots**. Test at night and near the
boat/beam intersection. Compare sun shadows on and off separately, enable Bloom after the base
lighting is proven, then inspect the shadow-map debug view for moving geometry. Test the island
toggle (lamp must go off), 0/1 PCF radius, spotlight bias, PBR off, different camera modes, and
OpenGL resize. Observe frame time before and after enabling `spotShadows` at a fixed camera.

### Actual validation recorded here

- `g++ -std=c++20 -Wall -Wextra -fsyntax-only` passed for `shadow_map.cpp`, `renderer.cpp`, and
  `application.cpp` using vendored headers and a temporary external missing-GLU-header shim.
  Only the baseline warning in `Application::run()` about the late `FrameContext::spot` aggregate
  field remained; the source files have no new compiler warnings.
- With Mesa's surfaceless EGL context (**OpenGL 4.5 Core Profile**, GLSL 4.10), all new or
  modified programs compiled **and linked**: PBR vertex/fragment, water vertex/fragment,
  shadow vertex/fragment and fullscreen/depth-debug pair.
- A synthetic PBR triangle on a 32×32 floating-point colour target verified the actual GPU
  branch and shadow sampling. At a sample pixel, red HDR brightness was **0.00000** with the
  lamp off, **0.70361** with the lamp on/unshadowed, and **0.00000** when its shadow sampler was
  given a nearer occluder depth. `glGetError() == GL_NO_ERROR`.
- These isolated tests did not run the complete course scene: the sandbox lacked the GLFW
  XRandR/GLU development packages. **Subsequently, the user ran B2 in the full application,
  discovered the lamp self-occlusion problem and confirmed the targeted fix.** No quantitative
  full-app performance measurements were provided.

### B2 report paragraph (adapt after visual testing)

> We added a second shadow-casting light sourced from the animated lighthouse node. Unlike the
> sun's parallel directional rays, the lighthouse emits a finite cone, so its shadow map uses a
> 1024×1024 **perspective** projection fitted to the outer cone angle and light range. The
> renderer first records the opaque scene's depths from the lamp, then shades the boat, island
> and Gerstner water with a local Cook–Torrance direct-light term, inverse-square attenuation
> and a smooth spotlight cone. Projected receiver positions are compared to the lamp depth
> texture with hardware PCF and slope-dependent bias. The spotlight has an independent depth
> bias because perspective depth is non-linear; this reduces self-shadow acne without
> over-offsetting the shadows. We reused the existing depth-map class and shadow shaders to
> limit duplicated code, at the cost of a second depth pass whenever the lamp casts shadows.
> The independent spotlight and shadow toggles allow comparison of no lamp, unshadowed lamp and
> shadowed lamp, while the debug view shows the actual perspective map. Bright illuminated areas
> pass into the existing HDR Bloom pipeline.

**References:** [LearnOpenGL: Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping)
for the depth rendering, bias, PCF and distinction between orthographic and perspective lights;
[LearnOpenGL: Light Casters](https://learnopengl.com/Lighting/Light-casters) for smooth cone
cutoffs and distance attenuation; course handout `final_project.html` and local `docs/TODO.md` B2.

### B2 integration fix — the emitting lamp was shadowing itself

When the user enabled **Lighthouse light** and **Spotlight shadows** at the same time, the
spotlight's contribution almost disappeared. This was **not** a light-intensity or shader-bias
problem: the solid emitting lamp geometry had its origin at the spotlight's position. Its +Z
extent was 0.65 m while the spotlight depth camera's near plane was 0.5 m, so the lamp's own
front face was written near the beginning of the perspective depth map. Water and the boat beyond
it then failed the comparison and appeared shadowed.

The user supplied a detailed correction report (`LIGHTHOUSE_SPOTLIGHT_SHADOW_FIX_DETAILS.md` in
the shared project exchange). The corrected **V2** source already contains the change:

- `SceneNode::castsSpotShadow` defaults to `true`; `SceneNode::collect()` forwards it to
  `DrawItem::castsSpotShadow`.
- Only `m_lamp->castsSpotShadow = false` in `island.cpp` excludes the emitting box from its own
  spotlight depth pass.
- `Renderer::renderDepthItems(..., spotlightPass = true)` checks the flag only while rendering
  the spotlight map. The regular sun/moon depth pass, normal PBR shading and the other lighthouse
  parts remain intact.

**Why this fix:** excluding just the luminous mesh corrects occluder selection without changing
its light origin, cone, intensity or global bias (which would cause Peter-Panning). It preserves
real shadows from the sail, hull, terrain and surrounding tower. The user confirmed the fixed
application now works; this is the authoritative full-scene verification. **B3 starts from that
fixed V2 baseline and leaves this logic untouched.**

## 4. B3 — Top-down render-to-texture minimap

**Status (2026-11-04):** originally implemented on V2 and tested in isolation; the user
subsequently verified B3 in the full application and merged it into V3 with A3. B3 does
**not** alter Track A code or the B2 shadow fix.

### Goal and pipeline

Provide a live picture-in-picture rendering from a **second camera**, not a cropped copy of the
main scene. The map should show the entire course, island, boat location and heading regardless
of whether the primary viewpoint is Chase, Deck, Orbit or Top.

```text
Existing simulation: RenderLists + FrameContext + MinimapInputs (course center/radius, boat pose)
  |
  +--> Normal camera: sun/spot depth passes -> main HDR RGBA16F -> Bloom -> ACES -> screen
  |
  +--> Minimap::camera(): independent top-down orthographic projection
         -> separate 256x256 (adjustable 128..512) HDR RGBA16F + depth24 target
         -> PBR opaque meshes + Gerstner water + course lines + heading arrow
         -> reuse post_frag.glsl with bloom OFF, ACES + gamma
         -> RGBA8 colour texture (own FBO, no depth needed in post pass)
         -> movable ImGui::Image window (UV vertical flip)
         -> for --capture only: unlit screen-space texture overlay in top-right
```

### Technical choices and justification

1. **Actual re-render, not a crop:** `Renderer::renderMinimap()` reuses the same Track A
   `RenderLists::opaque` and `lines` and the same PBR/water draw functions as the main scene.
   The island coastline, animated water, sail and buoy geometry therefore respond to simulation
   changes automatically. A downsampled screenshot of the main camera would not contain a
   useful top-down navigation view.
2. **Dedicated camera:** `Minimap::camera()` builds `glm::lookAt` from 500 m above the course
   centre, with `glm::ortho(-r, r, -r, r, 1, 1200)` and `r=1.25 × courseRadius` (minimum 10 m).
   This matches the original Top view scale but works independently of the active camera. We
   intentionally do **not** call `Cameras::update()` a second time; that function updates the
   main chase-camera smoothing state and would have introduced an unintended dependency.
3. **Two orientation modes:** fixed course mode has world **−Z up**, consistent with the existing
   Top camera. `minimapFollowsHeading=1` sets the top camera's up vector to
   `(sin(boatYaw), 0, cos(boatYaw))`, making the boat's forward direction point upward. The view
   remains centred on the course so the island and lap route stay in frame. This rotates the map
   but does not switch the primary camera or alter boat motion.
4. **Legible navigation marker:** at course scale the boat occupies only a handful of texels.
   A bright orange triangle and circular point are drawn as regular `LineBatch` geometry above
   the scene, with the tip aligned to the CPU boat yaw. Their world-space size makes direction
   legible at all map resolutions; they are **only** added to the minimap draw, not the original
   simulation data or normal camera.
5. **A separate HDR scene target followed by LDR texture:** the project already shades in
   linear HDR and applies exposure/ACES/gamma in `post_frag.glsl`. Rendering straight into RGBA8
   would clip bright lamp/sky values and bypass the shared tone mapping. We reuse `HdrTarget` for
   map colour and depth and generalise `Renderer::tonemap()` to choose its input and destination.
   The second display FBO uses RGBA8 (low memory, suitable for ImGui). Only one new GL texture
   is required for the final LDR image; its **name is stable across resizing**, because ImGui
   records the texture ID before the render pass updates the storage in that same frame.
6. **Deliberate pass simplification:** no additional sun or spotlight *depth rendering*, and
   map shading disables shadow lookups, distance fog, the skybox, wireframe water and Bloom.
   The map still uses lit PBR materials and real animated water for spatial recognition. We omit
   particles/other extra transparent passes: a dense, overlapping spray effect would clutter a
   256-pixel navigation view and duplicate relatively expensive draw work. The renderer restores
   default framebuffer and full viewport after the map, so ImGui and capture work normally.
7. **Every-frame update:** the map updates each frame for smooth heading/course movement and
   simplicity. The small target reduces **fragment** cost but not all geometry/vertex cost:
   duplicating scene draws is the principal performance trade-off. A later optimisation would
   redraw every second frame or use simplified map-only geometry if real measurements show a
   bottleneck. We do not claim an unmeasured FPS benefit.
8. **Report capture support:** `Application::run()` takes scripted screenshots **before** the
   ImGui backend draws. As a result `ImGui::Image()` is not in default `--capture` screenshots.
   When a capture is requested, `drawMinimapCaptureOverlay()` copies the already-tone-mapped
   RGBA8 texture to a top-right screen-space viewport with a small frame, using a separate
   `minimap_overlay_frag.glsl` sampler (no second tone-map or gamma). Normal interactive use
   retains the freely movable ImGui map window; this overlay runs **only** during screenshots.

### Changed files and responsibilities

| File | B3 changes |
|---|---|
| `src/minimap.h/.cpp` | Own orthographic camera maths, independent HDR target, stable GL_RGBA8 colour texture and display FBO; manage size and cleanup. |
| `src/renderer.h/.cpp` | Reuse opaque/water/line passes with map camera; add marker, re-target existing ACES post pass and support a screenshot-only map overlay. |
| `shaders/minimap_overlay_frag.glsl` | Copy already-tone-mapped map into the default framebuffer without changing its colours. |
| `src/feature_toggles.h` | `minimap`, `minimapSize` (128..512, default 256), `minimapFollowsHeading`. |
| `src/application.cpp` | Add UI controls, a separate movable ImGui map window, `--set` settings, a second render call and capture overlay condition. |
| `CMakeLists.txt` | Add `src/minimap.cpp`; reconfigure to copy the new shader. |
| `docs/TODO.md`, this file and `README.md` | Record the status, rationale, test coverage and usage. |

### Verification performed

- **Source checks:** `g++ -std=c++20 -fsyntax-only` passed for `src/minimap.cpp`,
  `src/renderer.cpp` and `src/application.cpp`. A temporary local shim supplied the missing
  system `GL/glu.h`; no project dependency was modified.
- **Camera maths:** compiled tests of multiple boat yaw values (`0, 0.6, 1.5, 3.0` radians)
  verified heading-up projects the boat's forward axis vertically upward with zero horizontal
  drift. A fixed-orientation test verified −Z maps to screen-up; the course centre projects to
  the image centre in both modes.
- **Real OpenGL framebuffer test:** Mesa OpenGL 4.5 Core (surfaceless EGL) accepted both the
  floating-point HDR+depth FBO and the RGBA8 FBO as complete. A filled HDR pixel read back as
  red **3.0** without clipping, and RGBA8 clearing to (0.3, 0.6) returned byte values
  **(76, 153)**. Resizing from 256 to 384 preserved the ImGui texture name and FBO completeness.
- **Real OpenGL renderer test:** the actual renderer, sky, water, PBR, post, debug and new overlay
  GLSL programs compiled/linked under Mesa. An emissive red box and Gerstner-water background
  were rendered with the second camera. A sample pixel was **(254,26,38)** over the red box
  compared to **(12,42,82)** over the unobstructed water after removing the box. The screenshot
  overlay returned **(12,42,82)**, restored the 640×480 main viewport, and GL reported no errors.
- **Limitations of the sandbox test:** the *complete GLFW/ImGui application* did not run
  here because the environment lacks XRandR and GLU development headers. Interactive visual
  checks were subsequently completed by the user before merging B3 into V3. Actual FPS
  measurements and full-app B3 screenshots were not supplied; capture commands follow.

### How to test and capture B3

Run a Debug/Release build and confirm: map toggles on/off, map window can move, resolution
slider resizes the image without a blank frame, fixed −Z-up versus heading-up rotates correctly
when the boat turns, the orange arrow agrees with the boat and the course/island stay visible.
Check the primary camera and existing B1/B2 effects are unchanged, especially at 22:00 when the
lighthouse is active. Try a small/large application window and compare performance with minimap
on/off. **Report screenshot commands** (Windows example; `--set minimapSize=256` is optional):

```powershell
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b3_minimap_off.png --frames 240 --camera chase --time 15.5 --set minimap=0
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b3_minimap_on.png --frames 240 --camera chase --time 15.5 --set minimap=1
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b3_heading_up.png --frames 240 --camera chase --time 15.5 --set minimap=1 --set minimapFollowsHeading=1 --set minimapSize=320
```

These captures include the map overlay but not other ImGui controls. Interactive screenshots
(using an operating-system screenshot tool) can show the movable ImGui window and its controls.
Do **not** claim these examples are screenshots already produced by the sandbox.

### B3 draft report paragraph

> We implemented a live minimap using a second, independent top-down **orthographic** camera
> and render-to-texture framebuffers. The course is projected from above into a small HDR colour
> and depth target, reusing the same PBR objects, procedural island, water and course lines as
> the main camera. The map is tone-mapped with the existing ACES pass into a display-ready RGBA8
> texture and shown in a movable ImGui window. A heading marker indicates the boat's orientation;
> the user can switch from fixed course-up to boat-heading-up orientation. We skip redundant
> shadow-map construction, Bloom, skybox and particles to limit the overhead of rendering the
> scene twice, and we use a small image resolution. Re-rendering the scene is more expensive
> than drawing a static 2D diagram, but it remains correct as the world animates and directly
> demonstrates an additional camera and off-screen rendering technique. We also added a
> capture-only overlay because the project's scripted screenshot path runs before ImGui.

**References:** course handout `final_project.html` (Minimap extra effect), `docs/TODO.md` B3,
[LearnOpenGL — Framebuffers](https://learnopengl.com/Advanced-OpenGL/Framebuffers) (texture
colour/depth attachments, framebuffer completeness, off-screen rendering, viewport changes),
`src/camera.cpp` (existing orthographic Top view) and `framework/third_party/imgui/include/imgui/imgui.h`
(`ImGui::Image` UV/texture interface). See also GLM `glm::lookAt`/`glm::ortho` APIs.

## 5. Upcoming Track B feature (not implemented in this change)
- **B4 Crest foam:** derive the Jacobian determinant of horizontal Gerstner displacement in the
  water vertex shader; vary foam with noisy breakup and wind intensity. Keep shader-side and CPU
  wave calculations consistent where needed.


## 5. B4 — Gerstner crest foam (V3: A3+B3 baseline)

**Status (2026-11-04):** B4 implemented on the V3 `main` baseline (`a1e8b61`) as an isolated
patch. The modified C++ files passed syntax checks, and both new/modified shaders compiled and
linked in a real Mesa OpenGL 4.5 Core context. A small GPU rasterisation test confirmed that
foam disappears on flat water, appears under normal waves, becomes stronger with wave height,
and can be switched on/off. **The complete GLFW/ImGui application and visual appearance on the
teammates' Windows machines remain to be tested before merging B4.**

### Purpose and derivation

The goal from `docs/TODO.md` §B4 is to show irregular whitewater at the **compressed crests**
of the existing Gerstner waves, becoming more visible at higher wind and wave height. A
simple test on wave height (e.g., y above zero) would draw foam over every high part, regardless
of whether the water was actually compressed. Instead we use the horizontal **Jacobian determinant**
of the Gerstner displacement, a simplified form of the folding/compression diagnostic commonly
used with choppy ocean surfaces (see the course plan's Tessendorf reference).

For a wave with horizontal unit direction **D = (Dx,Dz)**, amplitude `A`, steepness `Q`,
wavenumber `k`, phase angle `theta = k dot(D,p) - omega*time + phase`, its displaced horizontal
position is

```
X = x + sum_i Q_i * A_i * Dx_i * cos(theta_i)
Z = z + sum_i Q_i * A_i * Dz_i * cos(theta_i)
```

Differentiate **X,Z** with respect to the *undisplaced* coordinates **x,z**:

```
dX/dx = 1 - sum_i Q_i * A_i * k_i * Dx_i^2     * sin(theta_i)
dZ/dz = 1 - sum_i Q_i * A_i * k_i * Dz_i^2     * sin(theta_i)
dX/dz = dZ/dx
      =   - sum_i Q_i * A_i * k_i * Dx_i * Dz_i * sin(theta_i)
J = (dX/dx) * (dZ/dz) - (dX/dz)^2
```

`J=1` on an undeformed or calm surface, `J<1` means its horizontal patch is compressed,
and `J<=0` would mean an actual fold. The amplitude/steepness numbers come **directly from
`Waves::update()`** via existing `waveDirAmpK` / `waveOmegaPhaseQ` uniforms, so wind strength,
wave count and `waveScale` influence foam without introducing a separate water simulation.
In particular `Q*k*A` scales with `windStrength * waveScale` in this project; stronger winds
create more compressed crests. The existing `src/waves.cpp` displacement and normal equations
remain **unchanged**, preserving agreement between the drawn ocean and the floating boat.

### Implementation and design choices

1. **Analytic, single-pass vertex calculation.** In `shaders/water_vert.glsl`, the three
   independent components of the 2×2 horizontal deformation gradient are accumulated **inside
   the existing Gerstner loop**, sharing its sine, direction and wave parameters. Computing a
   Jacobian from screen-space `dFdx`/`dFdy` would mix in the camera projection and mesh
   tessellation; this is an actual derivative with respect to *rest-space water coordinates*.
   No extra draw pass, mesh, CPU water simulation or texture allocation is required.
2. **Soft threshold:** the vertex outputs
   `fragFoam = 1 - smoothstep(foamThreshold - 0.16, foamThreshold, J)`.
   The `smoothstep` avoids sharp on/off flicker as crest compression changes. Default
   `foamThreshold=0.86` means foam appears only at relatively strong compression;
   the Water UI exposes a 0.55–0.99 slider, with high thresholds producing more foam.
   `Renderer::drawWater()` clamps the value to this range for malformed CLI input.
3. **Break up the foam, do not create an unbroken white stripe.** `water_frag.glsl` samples the
   already-present **tileable water detail normal map** twice at different world-space scales,
   with a small wind-aligned scroll. The sampled red/green channels act as irregular coverage
   noise; `smoothstep(0.34, 0.66, 0.55*n1 + 0.45*n2)` cuts gaps in the analytic crest mask.
   The map remains bound even when detail normal *shading* is disabled, so the foam toggle is
   independent of the normal-mapping toggle. Using **world-space `fragPosition.xz`** prevents
   camera-following grid snaps from dragging the pattern around; scrolling allows mild foam
   evolution without a simulation history buffer.
4. **Lighting and compositing:** foam is treated as approximately diffuse whitewater, not a
   reflective Fresnel/GGX surface. After the original water body, sky reflection, sun specular
   and B2 lamp highlights, calculate foam radiance from ambient sky + shadowed directional
   sunlight + any unoccluded lighthouse spotlight. Composite
   `color = mix(color, foamColor * foamLight, foamMask)` **before distance fog**.
   This stops white foam glowing uniformly at night and preserves the existing shadowing,
   lighting, Bloom/HDR and fog passes. Water *does not start casting new shadows*.
5. **Branch isolation, no Track A rewrites:** only `shaders/water_vert.glsl`,
   `shaders/water_frag.glsl`, the two B-only renderer uniform lines in `src/renderer.cpp`,
   and the B4 settings in `src/feature_toggles.h` / `src/application.cpp` are needed for the
   feature. A3's new sail-blend code, A2's spray particles and the B2 spotlight-shadow fix
   are preserved untouched. No new `.cpp` or shader file needs to be added to CMake.
6. **Minimap:** the existing minimap pass reuses `Renderer::drawWater()`, so it inherits B4
   automatically. It continues using its own camera/targets and skipping expensive shadow
   passes. At coarse minimap resolution some tiny streaks may disappear by undersampling.

**Alternative approaches considered:** a height cutoff would mistake high but uncompressed
swell for breaking water; an independent animated white noise mask would not reliably follow
crests; a temporal advect/decay foam simulation would look more realistic but require tracking
world-space foam persistence and ping-pong GPU storage across frames, beyond B4's scope.
This is a **stylised instantaneous foam cue**, not a fluid-breaking or foam-advection model.
A further trade-off is that `fragFoam` is interpolated across the water mesh; far from the
camera the increasingly coarse grid can smooth away thin crests. We have not measured a
full-app frame-rate impact yet.

### Files changed

| File | Reason |
|---|---|
| `shaders/water_vert.glsl` | Compute the analytic horizontal Gerstner Jacobian in the existing wave loop and pass a soft foam mask. |
| `shaders/water_frag.glsl` | Break up the mask with two normal-map samples and apply ambient, sun and spotlight lighting before fog. |
| `src/renderer.cpp` | Pass `useFoam` and bounded `foamThreshold` to water shader for both main and minimap render calls. |
| `src/feature_toggles.h` | Add `foam` and `foamThreshold` configuration fields. |
| `src/application.cpp` | Water panel checkbox/slider and `--set foam=...`, `--set foamThreshold=...` for repeatable comparisons. |
| `README.md`, `docs/TODO.md`, `docs/ARCHITECTURE.md`, this file | Instructions, feature status, maths, trade-offs, verification and report draft. |

### Reproducible verification performed

- `g++ -std=c++20 -fsyntax-only` succeeded for `src/application.cpp`, `src/renderer.cpp` and
  `src/waves.cpp` using the vendored include files. A **temporary external shim** for missing
  system `GL/glu.h` was used for this syntax check only; no project source or dependencies
  were changed for it.
- Actual `water_vert.glsl` + `water_frag.glsl` (GLSL `#version 410`) compiled and linked on
  **Mesa 25.0.7, OpenGL 4.5 Core**, using a surfaceless EGL context. A second diagnostic
  fragment shader was linked with the *same B4 water vertex shader* to read `fragFoam`.
- A 240×240-cell test ocean grid spanning ±60 m was rendered into a 128×128 RGBA16F FBO.
  It used the same six-wavelength Gerstner spectrum and default wind strength `0.6` as
  `src/waves.cpp`, with `foamThreshold=0.86` and a synthetic RG ripple-texture sample for
  coverage noise. **Diagnostic mean foam mask** at wave height `0`, `1`, `2` was respectively
  **0.0000**, **0.0205**, **0.1199**. Pixels with mask above `0.1` were respectively
  **0.0%**, **5.0%**, **18.7%**.
- Running the actual fragment shader with foam *off/on* changed mean linear RGB by
  **0.0000 / 0.0102 / 0.0610** for wave height `0 / 1 / 2` in the same view and lighting.
  Flat water showed no difference and greater height produced stronger foam. Raising
  `foamThreshold` from `0.70` to `0.96` increased the HDR output. No GL errors were reported.
- These are **isolated GPU regression tests**, not screenshots or performance claims about
  the assembled sailboat app. The app cannot be fully built in this Linux test container
  because native XRandR/GLU development headers are absent.

### Branch-level test plan and report screenshots (Windows)

Start from the V3 `main` merge commit containing A3+B3. Apply the B4 patch on a feature branch
(`B4` or `B4. Crest Foam`), build, and expand **Water** in the UI.

Check: foam toggles off/on independently; wave height **0** and `waves=0` produce no foam; wave
height **1 vs 2** visibly differs; wind strength 0 vs 1 changes the effect; threshold increases
coverage; detail normal mapping off does not disable foam; sun/spot shadow toggles and the B3
minimap continue working. Compare calm/rough water and day/night, and confirm there is no
unexpected colour seam when the camera moves. Check GPU FPS with B4 on/off before claiming
performance costs.

The capture arguments below fix the time, wind and frame count. Turn off minimap to isolate the
water effect; keep all other flags equal between paired screenshots. Use a camera angle where
waves are clearly visible (Orbit or Chase), preferably with `--time 15.5` daylight.

```powershell
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b4_off.png --frames 240 --camera orbit --time 15.5 --set minimap=0 --set foam=0 --set waveScale=2 --set bloom=0
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b4_on.png --frames 240 --camera orbit --time 15.5 --set minimap=0 --set foam=1 --set foamThreshold=0.86 --set waveScale=2 --set bloom=0
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b4_default_waves.png --frames 240 --camera orbit --time 15.5 --set minimap=0 --set foam=1 --set foamThreshold=0.86 --set waveScale=1 --set bloom=0
.\out\build\x64-Debug\Master_TechDemo.exe --capture screenshots/b4_flat_water.png --frames 240 --camera orbit --time 15.5 --set minimap=0 --set foam=1 --set waveScale=0 --set bloom=0
```

`--capture` omits ImGui, so take an **interactive** screenshot with the Water panel expanded
if the final report needs to demonstrate the controls. These images have **not** been captured
in the sandbox and should not be represented as completed report screenshots.

### Draft B4 report passage

> We added foam to the Gerstner ocean by measuring compression in the wave's horizontal
> displacement. The water vertex shader analytically accumulates the 2×2 horizontal
> deformation gradient while calculating the same Gerstner waves used by the boat's
> buoyancy simulation. The determinant of this matrix, `J`, measures local surface-area
> compression: `J=1` on flat water and lower values indicate converging crests. We map
> this quantity to foam coverage using a tunable smooth threshold, then modulate it with
> two scrolling samples of our existing tileable ripple-normal texture to prevent solid
> white stripes. The foam is shaded using ambient skylight, shadowed sunlight and the
> lighthouse light, and blended with the HDR water colour before fog and tone mapping.
> This shader-only technique makes foam respond to wind and wave-height changes at low
> implementation complexity without changing the waves, buoyancy or render targets.
> It is an instantaneous stylised effect: it does not simulate how foam persists,
> drifts or decays after a wave breaks.

**References:** `docs/TODO.md` §B4 (the project-proposed Jacobian foam method), the
course-provided `final_project.html` (additional graphical technique, togglable UI),
`src/waves.cpp` / `src/waves.h` (existing wave parameters and normal), and the existing
`docs/ARCHITECTURE.md` §7 wave equations. For context, see Jerry Tessendorf,
*Simulating Ocean Water* (2001), and Mark Finch, *Effective Water Simulation from Physical
Models* (GPU Gems chapter 1, 2004). We implemented the per-vertex Gerstner Jacobian
approach above; we did **not** implement Tessendorf's FFT ocean or foam advection.
