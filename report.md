# 3D Graphics and Animation Tech demo | A Sailboat, an island and a light house 
Ethan de Beer and Gergo Vadasz
Team 55

## Work done:
We split the project into two tracks that could be built in parallel with few merge conflicts. Track A (Gergo Vadasz) covers the world and the simulation: new objects in the scene, their CPU logic and their own meshes and shaders. Track B (Ethan de Beer) covers the render pipeline: new passes, post-processing and lights. Before the split we built a shared skeleton with all seven minimal requirements. We also built a small interface (B0): draw hooks for opaque and transparent objects, a per-draw blend weight, a UV scale and offset in the PBR material, and slots in the UI. Because of it, no Track A feature had to edit the renderer or the shared shaders.

Each feature has one owner. It was built on its own branch and merged into main through a pull request reviewed by the other member. Every feature can be switched on and off in the UI and from the command line (--set name=0/1), so each on/off screenshot pair is one command.

| Feature | Gergo Vadasz | Ethan de Beer |
|---|---|---|
| Skeleton: the 7 minimal requirements, Gerstner water, buoyancy, constant speed, day-night, sun shadow map | [50] % | [50] % |
| B0 interface for parallel work | [50] % | [50] % |
| A1 Island and lighthouse | 100 % | 0 % |
| A2 Spray and wake particles | 100 % | 0 % |
| A3 Sail blend shapes | 100 % | 0 % |
| A4 Animated flag textures | 100 % | 0 % |
| A5 Free sailing and collisions | 100 % | 0 % |
| B1 Bloom | 0 % | 100 % |
| B2 Lighthouse spotlight and second shadow map | 0 % | 100 % |
| B3 Minimap | 0 % | 100 % |
| B4 Crest foam | 0 % | 100 % |
| B5 Stretch | 0 % | 100 % |
| Report and video | [50] % | [50] % |

## Track A - Gergo Vadasz
Track A adds five features to the scene. None of them changes the renderer. They draw through the B0 hooks and the shared PBR shader.

### A1 - Island and Lighthouse
I placed an island with a lighthouse on top of it in the middle of the map. It adds a second object to the hierarchy next to the boat, and gives B2 a source to work with.The lamp in the lighthouse is turning constantly, fades in at dusk and fades out at dawn. At night, it glows via the bloom pass implemented in B1. Toggles: island, lighthouseSpeed.

![The procedural island by day](<screenshots/Island and lighthouse/a1_island_day.png>)

*Figure 1. The island by day: fBm relief with a radial falloff. Sand, grass and layered rock are
chosen from height and slope.*

![The lighthouse at dusk](<screenshots/Island and lighthouse/a1_lighthouse_dusk.png>)

*Figure 2. At dusk the lamp fades on. Its HDR emission feeds the bloom (B1), and its spotlight (B2)
lights the water next to the boat.*

The lighthouse is built in code as Island → {Terrain, Lighthouse → Tower (+ Bands) → Gallery → LampHouse → Lamp}. The lamp's animation matrix is R_y(θ). The angle θ is integrated from the speed slider, so moving the slider never makes the beam jump. lampLight() turns the Lamp node's world matrix into a spotlight: its position is world[3], and its direction is the lamp's +z axis, tilted down to reach the water where the boat goes when it is tied to the course. 

![The beam sweeping the course, seen from above](<screenshots/Island and lighthouse/a1_beam_sweep_top.png>)

*Figure 3. Top view at night, 1.5 s apart. The Lamp node turns, and the spotlight built from its world
matrix sweeps round the course. The red dot is the boat.*

We generate the terrain so it needs no external assets and can be shaped exactly around the course. Its shore radius is also the collision circle in A5. We bake the materials into textures instead of blending them per pixel in a separate terrain shader. That way the island uses the shared PBR shader unchanged, gets shadows, image-based lighting and normal mapping for free, and stays out of Track B's files. The cost is the top-down UV projection, which stretches the texture on the steepest cliffs. Triplanar mapping [3] would fix that, but it needs its own shader. The lamp is emissive, with an HDR colour well above the bloom threshold. It is left out of its own spot shadow map: the light sits inside the lamp box, whose faces would otherwise block the whole cone.

### A2 - Spray and Wake 
Spray flies up from the bow and a foam wake trails behind the boat. More spray is thrown up when the boat is faster and when the bow hits the waves harder. The droplets drift with the wind and fall back into the sea. The foam floats on the waves and spreads out before it fades. A fixed pool of 4000 particles holds the 450–1500 that are usually alive. Toggles: particles, particleRate.

![Bow spray and foam wake, with particles on and off](<screenshots/Spray and wake particles/a2_particles_on_off.png>)

*Figure 4. The same moment with particles on (left) and off (right). B4's crest foam is switched off
in both, so only the particles differ.*

The two emitters are nodes without a mesh, Bow and Stern, in the boat's hierarchy, so they follow its heave, pitch and heel. To draw, the particles are sorted back to front and uploaded into one instance buffer. A single glDrawArraysInstanced call draws one quad per particle (glVertexAttribDivisor = 1). Spray quads face the camera, spanned by its right and up vectors while foam quads lie flat. The fragment shader draws a soft procedural puff with premultiplied alpha [5]. It lights the puff with wrapped diffuse light [6] and a forward-scattering lobe, so spray glows against a low sun. [4] is the classic reference for particle systems.

OpenGL 4.1, our limit on macOS, has no compute shaders. About a thousand particles are cheap on the CPU, and the CPU already has the wave-height query. A fixed pool means nothing is allocated after start-up, and the whole pool is uploaded in one call. Instancing sends 28 bytes per particle instead of four full vertices, and needs one draw call. We chose sorted "over" blending rather than additive blending. Additive blending needs no sort, but washes spray out to white by day and makes it glow at night. Order-independent transparency [7] would need extra render targets in the renderer, while sorting this few particles is cheap and exact. One artefact remains: a faint straight edge where a large quad cuts through the hull or the water. Soft particles [8] would fade it, but need the scene depth as a texture.

### A3 - Sail Blend Shapes 
The sail fills with wind through a blend shape, which replaces the earlier trick of scaling the sail node in x. The base mesh is a flat triangle. The weight follows the wind strength, and its sign follows the side the boom is on, so the sail bulges to leeward on both tacks. The weight is smoothed over time, so during a tack or jibe the sail goes through flat to the other side instead of flipping. Toggles: sailShapeOverride and sailShapeWeight set the fullness by hand.

![The sail at blend weights 0, 0.5 and 1](<screenshots/Sail blend shapes/a3_weights_edge_on.png>)

*Figure 5. The sail seen almost edge-on at weights 0, 0.5 and 1. At 0 it is a flat sliver; at 0.5 and
1 the blend shape gives it camber and twist.*

Both shapes come from one generator, so they have identical topology. A second GPUMesh constructor uploads the base mesh as usual. It also uploads the per-vertex differences (target − base, for positions and normals) as two extra vertex attributes, at locations 4 and 5 in the same vertex array. The vertex shader computes p = p_base + w · Δp and n = n_base + w · Δn in object space, before the model matrix. The normal is normalised again in the fragment shader. The shadow pass's vertex shader applies the same delta, so the shadow changes shape with the sail. The weight goes from the SceneNode to its DrawItem and then to a per-draw uniform. Meshes without a blend shape leave attributes 4 and 5 disabled, so OpenGL supplies (0, 0, 0) and one shader serves every mesh.

The old scale matrix had three problems:

- it could only stretch or mirror one bulge
- it needed the inverse-transpose normal matrix
- it became singular at zero billow, so it was clamped, and the sail jumped when it changed sides

A blend shape can store any deformation with the same topology, here camber and twist together. It costs 24 bytes per vertex and one multiply-add. We considered three alternatives:

- skinning, which suits jointed motion more than a smooth billow
- a cloth simulation [11], which would need collisions with the mast and boom and a stable integrator
- morphing on the CPU, which re-uploads the mesh every frame

Linear blending is exact for the camber, but only approximate for the twist, which is a rotation. At weight −1, the full sail on the other side, the head is a few centimetres from a true mirror image. Future work: a third shape for luffing, the sail flapping when the boat points into the wind. References: [9, 10].

### A4 - Animated Flag Textures 
We put our countries' flags on the stern of the boat. Their texture is a flipbook of 4 × 4 frames of the cloth rippling. It is stepped through at a rate set in the UI and raised by the wind: fps · (0.25 + 0.75 · wind strength). The flags are delayed in their frame starts so they don't mirror each other in their animation. Both flags swing round their staffs to stream downwind. The UI can freeze the flipbook on any frame, which makes two-frame screenshots easy. Toggles: flag, flagFps, flagFreeze, flagFrame.

![The flags at flipbook frames 0 and 8](<screenshots/Animated flag textures/a4_flag_frames_0_8.png>)

*Figure 6. The flags frozen at frames 0 and 8. Only `uvOffset` differs, and the ripple is half a cycle
apart.*

The frame depends on simulation time, not on how many frames were rendered. Stepping one atlas frame per rendered frame would make the flag flutter twice as fast at 120 fps as at 60 fps, and stall when the frame rate drops. We integrate the phase instead of computing ⌊t · fps⌋ directly. The two are the same at a fixed rate, but with the integrated phase, moving the slider or a change in wind never makes the animation jump. With one atlas and moving UVs, the texture is bound once and only one vec2 uniform changes per frame, instead of swapping between 16 textures. A texture array would avoid bleeding altogether, but needs a different sampler in the shared shader. The gutters keep bilinear filtering and mipmaps [12, 13] from mixing neighbouring frames into the edges. The cloth's geometry does not move, apart from the swing. A vertex wave, or a blend shape like A3's, would add a real flap.

### A5 - Free Sailing and Collision
With free sailing on (the F key or the UI), the player steers with A/D or the arrow keys instead of following the Bézier course. The boat can then hit the five buoys and the island:

- it bounces off
- the buoy tips away and rocks back for a few seconds
- a burst of spray (A2) shoots up from the contact point

Switching free sailing off puts the boat back at the nearest point of the course. This is our "collision between the player and objects" feature. Toggle: freeSailing.

![The boat hitting a buoy, in four frames](<screenshots/Free sailing and collisions/a5_buoy_hit_sequence.png>)

*Figure 7. A hit at 4.6 m/s, over 0.9 s. The boat is pushed out and bounces back with 30 % of the
speed it hit with, spray bursts at the contact point, and the buoy rocks as a damped spring.*


The boat's velocity is split into a part along its heading and a sideways part:

- the heading part moves towards the speed the existing polar gives for the boat's angle to the wind, with a time constant of 2.5s (the sail)
- the sideways part decays towards zero with a time constant of 0.5 s (the keel)
- the yaw rate is proportional to the rudder angle times the forward speed. This gives a constant turning circle of about 17 m, and no turning when the boat is stopped

The response pushes the boat out along the contact normal by the overlap. It reflects the normal part of the velocity with a restitution of 0.3, and keeps the tangential part, so a glancing hit slides past [14, 15]. The buoy's tilt is the closed-form response of a damped spring to an impulse, A · e^(−t/τ) · sin(ωt), evaluated at the time since the hit.

One circle would be too wide or too short for a long hull. An oriented box or a test against the mesh is much more code for six obstacles. Two circles need only a distance test and give the contact normal for free. Pushing out and reflecting needs no stiff penalty springs to tune, and never lets the boat sink into an obstacle. Checking once per frame is enough. At the speed slider's maximum of 15 m/s and the clamped time step of 0.1 s, the boat moves 1.5 m per step. That is far less than the 3.9 m it would need to pass through a buoy, so continuous collision detection is not needed. A physics engine would add a dependency without connecting to the wind and polar model that the rest of the demo uses. The buoy's closed-form rocking is exact and stable at any frame rate. The limitations are: 

- off-centre hits don't spin the boat (there is no moment of inertia)
- the island is a single circle, so in its bays the boat stops short of the beach
- the boat jumps back onto the course when free sailing is switched off


## Track B - Ethan de Beer

### B1. Bloom

We implemented bloom as an HDR post-processing effect to imitate the spread of light around bright sources, such as the sun, glints on the ocean and the lighthouse lamp. First, a soft-knee brightness threshold extracts highlights from the linear RGBA16F scene buffer at half resolution. A separable Gaussian filter blurs the result by alternating horizontal and vertical framebuffer passes. Bilinear sampling combines adjacent taps, reducing a nine-tap convolution to five texture fetches per direction. Finally, the blurred highlights are added to the scene before exposure and ACES tone mapping, preserving HDR intensities. We chose this method over a multi-resolution mip pyramid because it is compact, easy to parameterise and visually appropriate for the scene, although a pyramid would offer broader glow and potentially better performance for large radii. The UI exposes the threshold, intensity, iterations and a debug view so the effect can be evaluated independently. The implementation follows the HDR bloom structure in [18], with the existing filmic tonemapping fit [20]; the multi-scale method in [19] was considered but not adopted.

| Bloom disabled | Bloom enabled |
|:---:|:---:|
| ![Bloom disabled](<screenshots/Bloom/Bloom off.png>) | ![Bloom enabled](<screenshots/Bloom/Bloom on.png>) |

*Figure 8. Bloom off (left) and on (right) from the same deck view. The effect adds a subtle halo to the sun and bright reflections; the captured difference is small at this exposure.*

### B2. Lighthouse spotlight and second shadow map

We added a second shadow-casting light sourced from the animated lighthouse node. Unlike the sun's parallel directional rays, the lighthouse emits a finite cone, so its shadow map uses a 1024×1024 perspective projection fitted to the outer cone angle and light range. The renderer first records the opaque scene's depths from the lamp, then shades the boat, island and Gerstner water with a local Cook–Torrance direct-light term, inverse-square attenuation and a smooth spotlight cone. Projected receiver positions are compared to the lamp depth texture with hardware PCF and slope-dependent bias. The spotlight has an independent depth bias because perspective depth is non-linear; this reduces self-shadow acne without over-offsetting the shadows. We reused the existing depth-map class and shadow shaders to limit duplicated code, at the cost of a second depth pass whenever the lamp casts shadows. The independent spotlight and shadow toggles allow comparison of no lamp, unshadowed lamp and shadowed lamp, while the debug view shows the actual perspective map. Bright illuminated areas pass into the existing HDR Bloom pipeline. During integration, the lamp mesh initially occluded its own spotlight because the light originated inside it; we corrected this by excluding only that emitting mesh from the spotlight depth pass, while keeping it in the ordinary scene and directional-shadow passes. See [21, 22].


| Lighthouse light off | Lighthouse light with shadows |
|:---:|:---:|
| ![Lighthouse light off](<screenshots/Lighthouse spotlight and second shadow map/b2_light_off.png>) | ![Lighthouse light with shadows](<screenshots/Lighthouse spotlight and second shadow map/b2_light_shadowed.png>) |

*Figure 9. Nighttime scene with the lighthouse light off (left) and enabled with spotlight shadows (right). The lighting difference is subtle at this camera angle; the separate debug image below shows the perspective depth map.*

![Spotlight shadow-map debug view](<screenshots/Lighthouse spotlight and second shadow map/b2_shadow_debug.png>)

*Figure 9a. The spotlight shadow-map debug overlay visualises the light-view depth texture. The supplied unshadowed and shadowed scene screenshots are identical, so they do not by themselves demonstrate the shadow comparison.*

#### Visible lighthouse beam (B2 extra)

The B2 spotlight illuminates surfaces but does not make the light visible in empty air. We therefore generate three nested cone shells aligned with the rotating lamp's position, direction and outer angle. Their 32 radial segments and 18 longitudinal rings are built once, and the fragment shader fades intensity along the cone. We render them in the existing transparent-object pass with depth testing, disabled depth writes and additive blending (`SRC_ALPHA, ONE`), before Bloom. This is inexpensive and works with the scene's HDR buffer, but it is a stylised surface approximation rather than physically simulated scattering; objects do not cast volumetric shadows inside the beam. More complete light-shaft methods are discussed in [28]. Toggles: `spotBeam`, `spotBeamOpacity`.


| Visible beam off | Visible beam on |
|:---:|:---:|
| ![Visible beam off](<screenshots/extra_beam_off.png>) | ![Visible beam on](<screenshots/extra_beam_on.png>) |

*Figure 12. At night, enabling the translucent cone makes the rotating lighthouse beam visible in the air; the underlying spotlight still lights the scene in both cases.*

### B3. Minimap

We implemented a live minimap using a second, independent top-down orthographic camera and render-to-texture framebuffers. The course is projected from above into a small HDR colour and depth target, reusing the same PBR objects, procedural island, water and course lines as the main camera. The map is tone-mapped with the existing ACES pass into a display-ready RGBA8 texture and shown in a movable ImGui window. A heading marker indicates the boat's orientation; the user can switch from fixed course-up to boat-heading-up orientation. We skip redundant shadow-map construction, Bloom, skybox and particles to limit the overhead of rendering the scene twice, and we use a small image resolution. Re-rendering the scene is more expensive than drawing a static 2D diagram, but it remains correct as the world animates and directly demonstrates an additional camera and off-screen rendering technique. We also added a capture-only overlay because the project's scripted screenshot path runs before ImGui. The framebuffer and texture display interfaces follow the bundled GLM/ImGui APIs and the off-screen rendering pattern in [23–25].

| Minimap off | Minimap on |
|:---:|:---:|
| ![Minimap off](<screenshots/Minimap/b3_minimap_off.png>) | ![Minimap on](<screenshots/Minimap/b3_minimap_on.png>) |

*Figure 10. Identical sailing view with the minimap disabled and enabled. The top-down render is composited in the upper-right corner of the capture.*

![Heading-up minimap](<screenshots/Minimap/b3_heading_up.png>)

*Figure 10a. The heading-up mode rotates the minimap orientation with the boat while retaining a top-down orthographic projection.*

### B4. Crest foam

We added foam to the Gerstner ocean by measuring compression in the wave's horizontal displacement. The water vertex shader analytically accumulates the 2×2 horizontal deformation gradient while calculating the same Gerstner waves used by the boat's buoyancy simulation. The determinant of this matrix, J, measures local surface-area compression: J=1 on flat water and lower values indicate converging crests. We map this quantity to foam coverage using a tunable smooth threshold, then modulate it with two scrolling samples of our existing tileable ripple-normal texture to prevent solid white stripes. The foam is shaded using ambient skylight, shadowed sunlight and the lighthouse light, and blended with the HDR water colour before fog and tone mapping. This shader-only technique makes foam respond to wind and wave-height changes at low implementation complexity without changing the waves, buoyancy or render targets. It is an instantaneous stylised effect: it does not simulate how foam persists, drifts or decays after a wave breaks. The Gerstner-wave derivation follows the project wave model [17], with broader ocean-rendering context in [26, 27]; the temporal extension is described below under B5.

| Crest foam off | Crest foam on |
|:---:|:---:|
| ![Crest foam off](<screenshots/Crest Foam/b4_off.png>) | ![Crest foam on](<screenshots/Crest Foam/b4_on.png>) |

*Figure 11. The same rough-sea view without and with Jacobian-based crest foam: compression highlights the wave crests rather than all elevated water.*

#### Temporal crest foam (B4 Extra)

B4 only identifies crests at the present time. To let foam linger, we added two 512×512 R16F textures which ping-pong a history over a 230×230 m world-space region centred on the boat. For each texel the shader evaluates the same Gerstner-wave compression as B4, reprojects the previous history into the current region and updates it using `F_new = max(F_instant, F_previous × exp(-dt / lifetime))`. The default decay lifetime is 1.7 s. World-space history avoids the camera ghosting of screen-space accumulation; it is reset after a large jump or when waves are disabled. This approximates the birth and decay of whitecaps but does not advect individual foam patches or solve fluid dynamics [26, 27]. Toggles: `temporalFoam`, `foamLifetime`.


| Instantaneous foam | Temporal foam |
|:---:|:---:|
| ![Instantaneous foam](<screenshots/extra_foam_instant.png>) | ![Temporal foam](<screenshots/extra_foam_temporal.png>) |

*Figure 13. The original crest mask (left) compared with the history-accumulated foam (right). A single frame illustrates the coverage difference, while persistence is more apparent in motion.*

### B5. Stretch

In addition to B1–B4, we implemented both optional Track B extensions and the three B-stretch techniques proposed in our TODO [16, 17]. Each is independently toggleable. The more costly additions are disabled by default, allowing on/off comparisons without changing the original renderer.

#### Sailcloth translucency

The sail remains opaque but gains a thin-material transmission term so light arriving from its back can illuminate the viewer-facing side. Only the sail's PBR material has nonzero transmission. In the PBR shader we add a diffuse contribution proportional to `albedo × incoming radiance × transmission × max(0, dot(-N, L))`, including the sun's and the B2 spotlight's shadow visibility. The spotlight term is evaluated even when ordinary front-face lighting is zero, which is essential for backlighting. This retains A3 blend shapes, depth testing and normal shadow casting, and is substantially cheaper than refraction or subsurface scattering. It is an artistic thin-sheet model, not a fully physical cloth BRDF [29]. Toggles: `sailTranslucency`, `sailTransmission`.


| Transmission off | Transmission on |
|:---:|:---:|
| ![Transmission off](<screenshots/extra_sail_opaque.png>) | ![Transmission on](<screenshots/extra_sail_translucent.png>) |

*Figure 14. Backlit sailcloth without and with the thin-surface transmission term. The sail becomes brighter while retaining opaque depth and shadow behaviour.*

#### Cascaded directional shadows

One tight sun-shadow map gives the boat good resolution but cannot cover the distant island well. We added a second directional depth map and divided the perspective camera frustum into overlapping near (0.2–67 m) and far (43–370 m) regions, blending shadow visibility across the overlap. Each slice is fitted to an orthographic light projection, with light-space texel snapping to reduce shimmering. The orthographic top camera instead uses a boat-centred close map and a course-centred wide map, since a perspective-depth split would not suit it. This improves the resolution-versus-coverage trade-off, but adds an entire shadow pass, a second depth texture and possible transition artefacts; it is a basic two-cascade implementation rather than a full production CSM system [30]. Toggle: `cascadedShadows`.


| Single shadow map | Two cascades |
|:---:|:---:|
| ![Single shadow map](<screenshots/extra_shadow_single.png>) | ![Two cascades](<screenshots/extra_shadow_cascades.png>) |

*Figure 15. Single directional map versus the near/far cascaded maps. The differences are subtle in this view; the main benefit is balancing close-up shadow detail with distant course coverage.*

#### Screen-space ambient occlusion

We added an opaque-geometry depth/normal prepass, followed by half-resolution SSAO with 16 hemisphere samples around each reconstructed view-space position. A depth-aware 5×5 blur smooths the grayscale occlusion buffer. The PBR shader applies the result only to ambient/indirect lighting, leaving sunlight, the lighthouse, transmission, water and particles unoccluded by this pass. This gives an inexpensive approximation of contact shading compared with global illumination, but it is view-dependent, can miss hidden surfaces and is the most expensive new screen-space pass. Our screenshots also show strong sail darkening, which is a limitation to inspect rather than evidence of accurate global illumination [31]. Toggles: `ssao`, `ssaoRadius`, `ssaoStrength`, `showSsao`.


| SSAO off | SSAO on |
|:---:|:---:|
| ![SSAO off](<screenshots/extra_ssao_off.png>) | ![SSAO on](<screenshots/extra_ssao_on.png>) |

*Figure 16. Screen-space ambient occlusion disabled and enabled. The enabled capture substantially darkens the sail and nearby geometry; this strong response should be evaluated for over-occlusion rather than assumed to be physically correct.*

![SSAO debug mask](<screenshots/extra_ssao_mask.png>)

*Figure 16a. The SSAO debug view displays the ambient-visibility mask directly, allowing inspection of excessive darkening and screen-space artefacts.*

## References

### Track A references
1. K. Perlin, "An Image Synthesizer", SIGGRAPH 1985.
2. D. S. Ebert, F. K. Musgrave, D. Peachey, K. Perlin, S. Worley, *Texturing and Modeling: A
   Procedural Approach*, 3rd ed., Morgan Kaufmann, 2003 (fBm and procedural terrain).
3. R. Geiss, "Generating Complex Procedural Terrains Using the GPU", *GPU Gems 3*, ch. 1, 2007
   (triplanar texturing).
4. W. T. Reeves, "Particle Systems: A Technique for Modeling a Class of Fuzzy Objects", SIGGRAPH 1983.
5. T. Porter, T. Duff, "Compositing Digital Images", SIGGRAPH 1984 (premultiplied alpha, "over").
6. S. Green, "Real-Time Approximations to Subsurface Scattering", *GPU Gems*, ch. 16, 2004 (wrap
   lighting).
7. M. McGuire, L. Bavoil, "Weighted Blended Order-Independent Transparency", JCGT 2(2), 2013.
8. T. Lorach, "Soft Particles", NVIDIA whitepaper, 2007.
9. J. P. Lewis, K. Anjyo, T. Rhee, M. Zhang, F. Pighin, Z. Deng, "Practice and Theory of Blendshape
   Facial Models", Eurographics 2014 State of the Art Report.
10. T. Lorach, "DirectX 10 Blend Shapes: Breaking the Limits", *GPU Gems 3*, ch. 3, 2007.
11. D. Baraff, A. Witkin, "Large Steps in Cloth Simulation", SIGGRAPH 1998.
12. L. Williams, "Pyramidal Parametrics", SIGGRAPH 1983 (mipmapping).
13. NVIDIA, "Improve Batching Using Texture Atlases", SDK whitepaper, 2004.
14. C. Ericson, *Real-Time Collision Detection*, Morgan Kaufmann, 2004.
15. D. Baraff, "Physically Based Modeling: Rigid Body Simulation", SIGGRAPH course notes, 2001.

### Track B references

20. K. Narkowicz, [*ACES Filmic Tone Mapping Curve*](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/), 2016 (filmic tone-mapping approximation).
21. LearnOpenGL, [*Shadow Mapping*](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping) (depth maps, bias and PCF).
22. LearnOpenGL, [*Light Casters*](https://learnopengl.com/Lighting/Light-casters) (spotlight cone and attenuation).
23. LearnOpenGL, [*Framebuffers*](https://learnopengl.com/Advanced-OpenGL/Framebuffers) (off-screen render targets used by the minimap).
24. GLM, `glm::lookAt` and `glm::ortho` APIs (the version of GLM bundled with the project; orthographic minimap camera).
25. Dear ImGui, `ImGui::Image` API in `imgui.h` (the version bundled with the project; displaying the minimap texture).
26. J. Tessendorf, *Simulating Ocean Water*, SIGGRAPH course notes, 2001 (ocean-simulation background; we did not implement an FFT ocean).
27. M. Finch, [*Effective Water Simulation from Physical Models*](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models), *GPU Gems*, ch. 1, 2004 (Gerstner water context).
28. K. Mitchell, [*Volumetric Light Scattering as a Post-Process*](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process), *GPU Gems 3*, ch. 13, 2007 (background on volumetric light shafts; our beam uses simpler translucent cone geometry).
29. B. Karis, [*Real Shading in Unreal Engine 4*](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf), SIGGRAPH course notes, 2013 (physically based shading background, not a direct cloth-transmission model).
30. M. Valient, *Stable Rendering of Cascaded Shadow Maps*, *ShaderX6*, 2008 (stable cascade principles and reduced shadow shimmer; our implementation uses a simplified two-cascade fit).
31. J. Chapman, [*SSAO Tutorial*](https://john-chapman-graphics.blogspot.com/2013/01/ssao-tutorial.html), posted January 2013 (view-space depth sampling and hemisphere SSAO).
