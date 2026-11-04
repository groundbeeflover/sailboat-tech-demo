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
