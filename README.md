# Sailboat Tech Demo (CS4515 final project)

Real-time OpenGL 4.1 tech demo: a procedurally built sailboat races a Bezier buoy course on an endless ocean. One wind control drives the waves, sail, boom, heel and speed.

a time-of-day slider drives the procedural sky, environment map and lighting. Every feature can be toggled in the UI.

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
./build/Master_TechDemo
```
Thank you for looking at our demo! We hope you enjoy it, find it interesting and fun. 