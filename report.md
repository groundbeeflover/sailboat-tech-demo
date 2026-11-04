# 3D Graphics and Animation Tech demo | A Sailboat, an island and a lighthouse 
Ethan de Beer and Gergo Vadasz
Team 55

## Work done:
We split the project into two tracks that could be built in parallel with few merge conflicts. Track A (Gergo Vadasz) covers the world and the simulation: new objects in the scene, their CPU logic and their own meshes and shaders. Track B (Ethan de Beer) covers the render pipeline: new passes, post-processing and lights. Before the split we built a shared skeleton with all seven minimal requirements. We also built a small interface (B0): draw hooks for opaque and transparent objects, a per-draw blend weight, a UV scale and offset in the PBR material, and slots in the UI. Because of it, no Track A feature had to edit the renderer or the shared shaders.

Each feature has one owner and was built on its own branch. Most were merged into main through a pull request that the other member merged. Every feature can be switched on and off in the UI and from the command line (--set name=0/1), so each on/off screenshot pair is one command.

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
Track A adds five features to the scene. None of them changes the renderer or the shared shaders: they draw through the B0 hooks, with the shared PBR shader or, for the particles, their own shader pair.

### A1 - Island and Lighthouse
We placed an island with a lighthouse on top of it in the middle of the course. It adds a second object hierarchy next to the boat's, and its lamp is the source of B2's spotlight. The lamp turns constantly, fades on at dusk and off at dawn, and at night glows through the bloom pass from B1. Toggles: island, lighthouseSpeed.

![The procedural island by day](<screenshots/Island and lighthouse/a1_island_day.png>)

*Figure 1. The island by day: fBm relief with a radial falloff. Sand, grass and layered rock are
chosen from height and slope.*

![The lighthouse at dusk](<screenshots/Island and lighthouse/a1_lighthouse_dusk.png>)

*Figure 2. At dusk the lamp fades on. Its HDR emission feeds the bloom (B1), and its spotlight (B2)
lights the water next to the boat.*

The lighthouse is built in code as Island → {Terrain, Lighthouse → Tower (+ Bands) → Gallery → LampHouse → Lamp}. The lamp's animation matrix is R_y(θ). The angle θ is integrated from the speed slider, so moving the slider never makes the beam jump. lampLight() turns the Lamp node's world matrix into a spotlight: its position is world[3], and its direction is the lamp's +z axis, tilted down so that the beam reaches the water along the course. 

![The beam sweeping the course, seen from above](<screenshots/Island and lighthouse/a1_beam_sweep_top.png>)

*Figure 3. Top view at night, 1.5 s apart. The Lamp node turns, and the spotlight built from its world
matrix sweeps round the course. The red dot is the boat.*

The terrain is a height field of fractal value noise (fBm), faded into a flat seabed by a radial falloff so that it crosses the waterline at the coast. We generate it so it needs no external assets and can be shaped exactly around the course; its shore radius is also the collision circle in A5. Sand, grass and rock are chosen from height and slope and baked into textures, instead of being blended per pixel in a separate terrain shader. That way the island uses the shared PBR shader unchanged, gets shadows, image-based lighting and normal mapping for free, and stays out of Track B's files. The cost is the top-down UV projection, which stretches the texture on the steepest cliffs. Triplanar mapping [1] would fix that, but it needs its own shader. The lamp is emissive, with an HDR colour well above the bloom threshold. It is left out of its own spot shadow map: the light sits inside the lamp box, whose faces would otherwise block the whole cone.

### A2 - Spray and Wake 
Spray flies up from the bow and a foam wake trails behind the boat. More spray is thrown up when the boat is faster and when the bow hits the waves harder. The droplets drift with the wind and fall back into the sea. The foam floats on the waves and spreads out before it fades. A fixed pool of 4000 particles holds the 450–1500 that are usually alive. Toggles: particles, particleRate.

![Bow spray and foam wake, with particles on and off](<screenshots/Spray and wake particles/a2_particles_on_off.png>)

*Figure 4. The same moment with particles on (left) and off (right). B4's crest foam is switched off
in both, so only the particles differ.*

The two emitters are nodes without a mesh, Bow and Stern, in the boat's hierarchy, so they follow its heave, pitch and heel. As in a classic particle system [2], the particles are simulated on the CPU with the real frame time; the same Gerstner sum that the water shader draws, evaluated on the CPU, tells spray where to die and foam where to float. To draw, the particles are sorted back to front and uploaded into one instance buffer. A single glDrawArraysInstanced call draws one quad per particle (glVertexAttribDivisor = 1). Spray quads face the camera, spanned by its right and up vectors, while foam quads lie flat. The fragment shader draws a soft procedural puff with premultiplied alpha [3]. It lights the puff with wrapped diffuse light [4] and a forward-scattering lobe, so spray glows against a low sun.

OpenGL 4.1, our limit on macOS, has no compute shaders. About a thousand particles are cheap on the CPU, and the CPU already has the wave-height query. A fixed pool means nothing is allocated after start-up, and the whole pool is uploaded in one call. Instancing sends 28 bytes per particle instead of four full vertices, and needs one draw call. We chose sorted "over" blending rather than additive blending. Additive blending needs no sort, but washes spray out to white by day and makes it glow at night. Order-independent transparency [5] would need extra render targets in the renderer, while sorting this few particles is cheap and exact. One artefact remains: a faint straight edge where a large quad cuts through the hull or the water. Soft particles [6] would fade it, but need the scene depth as a texture.

### A3 - Sail Blend Shapes 
The sail fills with wind through a blend shape [7, 8], which replaces the earlier trick of scaling the sail node in x. The base mesh is a flat triangle; the target is a full sail with 10 % camber, deepest at 40 % of the chord, and 12° of extra twist at the head. A weight w blends between the two. It follows the wind strength, and its sign follows the side the boom is on, so the sail bulges to leeward on both tacks. The weight is smoothed over time, so during a tack or jibe the sail goes through flat to the other side instead of flipping. Toggles: sailShapeOverride and sailShapeWeight set the fullness by hand.

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
- a cloth simulation [9], which would need collisions with the mast and boom and a stable integrator
- morphing on the CPU, which re-uploads the mesh every frame

Linear blending is exact for the camber, but only approximate for the twist, which is a rotation. At weight −1, the full sail on the other side, the head is a few centimetres from a true mirror image. Future work: a third shape for luffing, the sail flapping when the boat points into the wind.

### A4 - Animated Flag Textures 
We put our countries' flags, Hungarian and South African, on the stern of the boat. Their texture is a flipbook of 4 × 4 frames of the cloth rippling. It is stepped through at a rate set in the UI and raised by the wind: fps · (0.25 + 0.75 · wind strength). The second flag runs five frames ahead, so the two don't flutter in step. Both flags swing round their staffs to stream downwind. The UI can freeze the flipbook on any frame (Figure 6). Toggles: flag, flagFps, flagFreeze, flagFrame.

![The flags at flipbook frames 0 and 8](<screenshots/Animated flag textures/a4_flag_frames_0_8.png>)

*Figure 6. The flags frozen at frames 0 and 8. Only `uvOffset` differs, and the ripple is half a cycle
apart.*

Each flipbook frame is one cell of a 4 × 4 atlas generated in code, with a 4-texel gutter around each picture that repeats its edge. On every rendered frame we advance a phase φ ← (φ + fps · dt) mod 16 and set the material's uvOffset to atlas cell ⌊φ⌋. The shared PBR shader already samples at uv · uvScale + uvOffset (B0), so the renderer does not change.

The frame depends on simulation time, not on how many frames were rendered. Stepping one atlas frame per rendered frame would make the flag flutter twice as fast at 120 fps as at 60 fps, and stall when the frame rate drops. We integrate the phase instead of computing ⌊t · fps⌋ directly. The two are the same at a fixed rate, but with the integrated phase, moving the slider or a change in wind never makes the animation jump. With one atlas and moving UVs, the texture is bound once and only one vec2 uniform changes per frame, instead of swapping between 16 textures. The gutters keep bilinear filtering and mipmaps [10, 11] from mixing neighbouring frames into the edges; a texture array would avoid this bleeding altogether, but needs a different sampler in the shared shader. The cloth's geometry does not move, apart from the swing. A vertex wave, or a blend shape like A3's, would add a real flap.

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

- the heading part moves towards the speed the existing polar gives for the boat's angle to the wind, with a time constant of 2.5 s (the sail)
- the sideways part decays towards zero with a time constant of 0.5 s (the keel)
- the yaw rate is proportional to the rudder angle times the forward speed. This gives a constant turning circle of about 17 m, and no turning when the boat is stopped

Collisions are tested in the water plane. The hull is two circles of radius 1.25 m, 2.2 m ahead of and behind amidships; the buoys (radius 0.7 m) and the island are circles too, so a hit is a distance between two centres below the sum of their radii. The response pushes the boat out along the contact normal by the overlap. It reflects the normal part of the velocity with a restitution of 0.3, and keeps the tangential part, so a glancing hit slides past [12, 13]. The buoy's tilt is the closed-form response of a damped spring to an impulse, A · e^(−t/τ) · sin(ωt), evaluated at the time since the hit.

One circle would be too wide or too short for a long hull. An oriented box or a test against the mesh is much more code for six obstacles. Two circles need only a distance test and give the contact normal for free. Pushing out and reflecting needs no stiff penalty springs to tune, and never lets the boat sink into an obstacle. Checking once per frame is enough. At the speed slider's maximum of 15 m/s, and with the time step clamped to at most 0.1 s, the boat moves 1.5 m per step. That is far less than the 3.9 m, 2 × (1.25 m + 0.7 m), it would need to pass clean through a buoy, so continuous collision detection is not needed. A physics engine would add a dependency without connecting to the wind and polar model that the rest of the demo uses. The buoy's closed-form rocking is exact and stable at any frame rate. The limitations are: 

- off-centre hits don't spin the boat (there is no moment of inertia)
- the island is a single circle, so in its bays the boat stops short of the beach
- the boat jumps back onto the course when free sailing is switched off


## Track B - Ethan de Beer

### B1. Bloom

We implemented bloom as an HDR post-processing effect to imitate the spread of light around bright sources, such as the sun, glints on the ocean and the lighthouse lamp. First, a soft-knee brightness threshold extracts highlights from the linear RGBA16F scene buffer at half resolution. A separable Gaussian filter blurs the result by alternating horizontal and vertical framebuffer passes. Bilinear sampling combines adjacent taps, reducing a nine-tap convolution to five texture fetches per direction. Finally, the blurred highlights are added to the scene before exposure and ACES tone mapping, preserving HDR intensities. We chose this method over a multi-resolution mip pyramid because it is compact, easy to parameterise and visually appropriate for the scene, although a pyramid would offer broader glow and potentially better performance for large radii. The UI exposes the threshold, intensity, iterations and a debug view so the effect can be evaluated independently.

### B2. Lighthouse spotlight and second shadow map

We added a second shadow-casting light sourced from the animated lighthouse node. Unlike the sun's parallel directional rays, the lighthouse emits a finite cone, so its shadow map uses a 1024×1024 perspective projection fitted to the outer cone angle and light range. The renderer first records the opaque scene's depths from the lamp, then shades the boat, island and Gerstner water with a local Cook–Torrance direct-light term, inverse-square attenuation and a smooth spotlight cone. Projected receiver positions are compared to the lamp depth texture with hardware PCF and slope-dependent bias. The spotlight has an independent depth bias because perspective depth is non-linear; this reduces self-shadow acne without over-offsetting the shadows. We reused the existing depth-map class and shadow shaders to limit duplicated code, at the cost of a second depth pass whenever the lamp casts shadows. The independent spotlight and shadow toggles allow comparison of no lamp, unshadowed lamp and shadowed lamp, while the debug view shows the actual perspective map. Bright illuminated areas pass into the existing HDR Bloom pipeline.

### B3. Minimap

We implemented a live minimap using a second, independent top-down orthographic camera and render-to-texture framebuffers. The course is projected from above into a small HDR colour and depth target, reusing the same PBR objects, procedural island, water and course lines as the main camera. The map is tone-mapped with the existing ACES pass into a display-ready RGBA8 texture and shown in a movable ImGui window. A heading marker indicates the boat's orientation; the user can switch from fixed course-up to boat-heading-up orientation. We skip redundant shadow-map construction, Bloom, skybox and particles to limit the overhead of rendering the scene twice, and we use a small image resolution. Re-rendering the scene is more expensive than drawing a static 2D diagram, but it remains correct as the world animates and directly demonstrates an additional camera and off-screen rendering technique. We also added a capture-only overlay because the project's scripted screenshot path runs before ImGui.

### B4. Crest foam

We added foam to the Gerstner ocean by measuring compression in the wave's horizontal displacement. The water vertex shader analytically accumulates the 2×2 horizontal deformation gradient while calculating the same Gerstner waves used by the boat's buoyancy simulation. The determinant of this matrix, J, measures local surface-area compression: J=1 on flat water and lower values indicate converging crests. We map this quantity to foam coverage using a tunable smooth threshold, then modulate it with two scrolling samples of our existing tileable ripple-normal texture to prevent solid white stripes. The foam is shaded using ambient skylight, shadowed sunlight and the lighthouse light, and blended with the HDR water colour before fog and tone mapping. This shader-only technique makes foam respond to wind and wave-height changes at low implementation complexity without changing the waves, buoyancy or render targets. It is an instantaneous stylised effect: it does not simulate how foam persists, drifts or decays after a wave breaks.

### B5. Stretch

## References

### Track A references
1. R. Geiss, "Generating Complex Procedural Terrains Using the GPU", *GPU Gems 3*, ch. 1, 2007
   (triplanar texturing).
2. W. T. Reeves, "Particle Systems: A Technique for Modeling a Class of Fuzzy Objects", SIGGRAPH 1983.
3. T. Porter, T. Duff, "Compositing Digital Images", SIGGRAPH 1984 (premultiplied alpha, "over").
4. S. Green, "Real-Time Approximations to Subsurface Scattering", *GPU Gems*, ch. 16, 2004 (wrap
   lighting).
5. M. McGuire, L. Bavoil, "Weighted Blended Order-Independent Transparency", JCGT 2(2), 2013.
6. T. Lorach, "Soft Particles", NVIDIA whitepaper, 2007.
7. J. P. Lewis, K. Anjyo, T. Rhee, M. Zhang, F. Pighin, Z. Deng, "Practice and Theory of Blendshape
   Facial Models", Eurographics 2014 State of the Art Report.
8. T. Lorach, "DirectX 10 Blend Shapes: Breaking the Limits", *GPU Gems 3*, ch. 3, 2007.
9. D. Baraff, A. Witkin, "Large Steps in Cloth Simulation", SIGGRAPH 1998.
10. L. Williams, "Pyramidal Parametrics", SIGGRAPH 1983 (mipmapping).
11. NVIDIA, "Improve Batching Using Texture Atlases", SDK whitepaper, 2004.
12. C. Ericson, *Real-Time Collision Detection*, Morgan Kaufmann, 2004.
13. D. Baraff, "Physically Based Modeling: Rigid Body Simulation", SIGGRAPH course notes, 2001.

### Track B references
