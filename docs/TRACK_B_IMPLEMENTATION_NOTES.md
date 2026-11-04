# Track B — renderer review, decisions and report notes

> Maintained as features are implemented. **B1 Bloom is implemented; B2–B4 are not yet implemented.**
> The full sailboat application still needs an end-to-end build and screenshots on a workstation
> with the project's OpenGL/GLFW development dependencies (see Validation below).
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

**Status:** implemented and tested in isolation with a real OpenGL context; full application
build, visual inspection of the *sailboat scene*, and report screenshots are still pending.

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

## 3. Upcoming Track B interfaces (not implemented in this change)

- **B2 Spotlight + shadow map:** consume `frame.spot` from Track A's lighthouse, with a temporary
  light only if testing B2 before A1 merges. Reuse PBR and water GGX/visibility logic; second
  perspective shadow map on unit 5. Keep directional sun shadow implementation intact. New
  controls in Track B slots.
- **B3 Minimap:** render a second camera to a colour target and display with ImGui. Reuse the
  draw lists from Track A without requiring Track A renderer changes.
- **B4 Crest foam:** derive the Jacobian determinant of horizontal Gerstner displacement in the
  water vertex shader; vary foam with noisy breakup and wind intensity. Keep shader-side and CPU
  wave calculations consistent where needed.
