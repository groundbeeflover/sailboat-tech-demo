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
| Report and video | [50] % | [50] % |

## Track A - Gergo Vadasz


## Track B - Ethan de Beer

## References