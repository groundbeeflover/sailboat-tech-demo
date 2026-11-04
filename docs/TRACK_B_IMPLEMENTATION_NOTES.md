# Track B — renderer review, decisions and report notes

> Maintained as features are implemented. **B1 Bloom and B2 Lighthouse spotlight are implemented; B3–B4 are not yet implemented.**
> A1+B1 was reported working together by the user on their workstation (2026-11-04). B2 has
> passed isolated validation but still needs an end-to-end application run and screenshots.
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

**Status (2026-11-04):** implemented in an isolated B2 patch on the supplied A1+B1 baseline;
C++ syntax checks and standalone real OpenGL validation passed. The complete Windows application
and night-scene screenshots have **not** been tested here, so merge only after the branch-level
visual check described below.

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
- This does **not** replace testing the complete course scene: the sandbox lacks the GLFW
  XRandR/GLU development packages for the full application. No real app screenshots or measured
  end-to-end frame-rate impact were produced, and real-world shadow bias may need tuning.

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

## 4. Upcoming Track B interfaces (not implemented in this change)
- **B3 Minimap:** render a second camera to a colour target and display with ImGui. Reuse the
  draw lists from Track A without requiring Track A renderer changes.
- **B4 Crest foam:** derive the Jacobian determinant of horizontal Gerstner displacement in the
  water vertex shader; vary foam with noisy breakup and wind intensity. Keep shader-side and CPU
  wave calculations consistent where needed.
