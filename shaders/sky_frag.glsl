#version 410

// Procedural sky, rendered once per cubemap face whenever the sun moves. The resulting cubemap is
// the environment map used by the skybox, the water reflections and the PBR ambient light.

uniform vec3 sunDirection;
uniform vec3 faceForward; // direction through the face centre
uniform vec3 faceRight; // direction of increasing s
uniform vec3 faceUp; // direction of increasing t
uniform float faceSize;

out vec4 fragColor;

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 skyRadiance(vec3 dir)
{
    float elevation = sunDirection.y;
    float day = smoothstep(-0.12, 0.15, elevation);
    // Sunrise/sunset: strongest when the sun is just around the horizon.
    float sunset = exp(-pow(elevation / 0.18, 2.0)) * smoothstep(-0.18, 0.0, elevation);

    vec3 zenith = mix(vec3(0.004, 0.006, 0.02), vec3(0.07, 0.20, 0.58), day);
    vec3 horizon = mix(vec3(0.015, 0.02, 0.04), vec3(0.50, 0.66, 0.88), day);
    vec2 flatDir = normalize(dir.xz + vec2(1e-5));
    vec2 flatSun = normalize(sunDirection.xz + vec2(1e-5));
    float towardsSun = pow(max(dot(flatDir, flatSun), 0.0), 3.0);
    horizon = mix(horizon, vec3(1.0, 0.42, 0.16), sunset * (0.3 + 0.7 * towardsSun));

    float up = max(dir.y, 0.0);
    vec3 sky = mix(horizon, zenith, pow(up, 0.45));

    // Sun glow and disk (HDR values well above 1).
    vec3 sunColor = mix(vec3(1.0, 0.5, 0.25), vec3(1.0, 0.95, 0.88), smoothstep(0.0, 0.35, elevation));
    float cosAngle = dot(dir, sunDirection);
    sky += sunColor * (0.3 * pow(max(cosAngle, 0.0), 10.0) + 0.6 * pow(max(cosAngle, 0.0), 300.0)) * day;
    sky += sunColor * 20.0 * smoothstep(0.99985, 0.99992, cosAngle) * smoothstep(-0.05, 0.02, elevation);

    // Night: stars and a moon opposite the sun.
    float night = 1.0 - smoothstep(-0.2, 0.0, elevation);
    if (night > 0.0 && dir.y > 0.0) {
        // Cells about one cubemap texel wide; a few of them light up with a random brightness.
        vec3 cell = floor(dir * 450.0);
        float star = step(0.9985, hash13(cell)) * (0.5 + 1.5 * hash13(cell + 17.0));
        sky += vec3(star * night * 2.0 * smoothstep(0.0, 0.2, dir.y));
        sky += vec3(0.8, 0.85, 1.0) * 3.0 * night * smoothstep(0.9997, 0.99985, dot(dir, -sunDirection));
    }

    // Below the horizon: fade to a dark haze so grazing reflections stay plausible.
    if (dir.y < 0.0) {
        vec3 below = mix(vec3(0.004, 0.006, 0.012), vec3(0.08, 0.13, 0.18), day);
        sky = mix(horizon, below, smoothstep(0.0, 0.15, -dir.y));
    }
    return sky;
}

void main()
{
    // Texel (s, t) of a cubemap face maps to direction forward + sc * right + tc * up with sc, tc in [-1, 1].
    vec2 st = gl_FragCoord.xy / faceSize * 2.0 - 1.0;
    vec3 dir = normalize(faceForward + st.x * faceRight + st.y * faceUp);
    fragColor = vec4(skyRadiance(dir), 1.0);
}
