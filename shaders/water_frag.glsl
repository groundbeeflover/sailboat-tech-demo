#version 410

in vec3 fragPosition;
in vec3 fragNormal;
in float fragHeight;
in float fragFoam;

uniform bool useFoam;

uniform vec3 cameraPosition;
uniform bool orthographic;
uniform vec3 viewForward;

uniform vec3 lightDirection; // towards the sun (or moon)
uniform vec3 lightColor;
uniform vec3 ambientColor; // used when the environment map is off

uniform samplerCube environmentMap;
uniform bool useEnvironmentMap;

uniform sampler2D detailNormalMap;
uniform bool useDetailNormals;
uniform float time;
uniform vec2 windDirection; // direction the small ripples drift in

uniform bool useFog;
uniform float fogDistance;

uniform bool useShadows;
uniform sampler2DShadow shadowMap;
uniform mat4 lightSpaceMatrix;
uniform int pcfRadius;
uniform float shadowBias;
// Independent B2 lighthouse direct light: finite cone, inverse-square falloff, own shadow map.
uniform bool useSpotlight;
uniform vec3 spotPosition;
uniform vec3 spotDirection;
uniform vec3 spotColor;
uniform float spotCosInner;
uniform float spotCosOuter;
uniform float spotRange;
uniform bool useSpotShadows;
uniform sampler2DShadow spotShadowMap;
uniform mat4 spotLightSpaceMatrix;
uniform float spotBias;


out vec4 fragColor;

const float PI = 3.14159265;

vec3 viewVector()
{
    return orthographic ? -viewForward : normalize(cameraPosition - fragPosition);
}

// Fog towards the sky colour just above the horizon in the viewing direction, so the far edge
// of the grid melts into the skybox.

// Match this function in the water shader: geometric normal determines depth bias.
float spotVisibility(vec3 worldPosition, float NdotL)
{
    if (!useSpotShadows)
        return 1.0;
    vec4 clip = spotLightSpaceMatrix * vec4(worldPosition, 1.0);
    if (clip.w <= 0.0)
        return 1.0;
    vec3 p = clip.xyz / clip.w * 0.5 + 0.5;
    if (p.z <= 0.0 || p.z >= 1.0 || any(lessThan(p.xy, vec2(0.0))) || any(greaterThan(p.xy, vec2(1.0))))
        return 1.0;
    float slope = clamp(sqrt(max(1.0 - NdotL * NdotL, 0.0)) / max(NdotL, 0.05), 0.0, 10.0);
    float bias = spotBias * (1.0 + slope); // independent: perspective depth is non-linear
    vec2 texel = 1.0 / vec2(textureSize(spotShadowMap, 0));
    float lit = 0.0;
    for (int x = -pcfRadius; x <= pcfRadius; ++x)
        for (int y = -pcfRadius; y <= pcfRadius; ++y)
            lit += texture(spotShadowMap, vec3(p.xy + vec2(x, y) * texel, p.z - bias));
    float side = float(2 * pcfRadius + 1);
    return lit / (side * side);
}

// Return attenuation, and the fragment-to-lamp vector needed by the BRDF.
float spotAttenuation(vec3 toLamp, out vec3 L)
{
    float r2 = dot(toLamp, toLamp);
    float distance = sqrt(r2);
    L = toLamp / max(distance, 1e-4);
    if (distance < 1e-4 || distance >= spotRange)
        return 0.0;
    float cone = smoothstep(spotCosOuter, spotCosInner, dot(-L, spotDirection));
    float fade = clamp(1.0 - pow(distance / spotRange, 4.0), 0.0, 1.0);
    return cone * fade * fade / max(r2, 1.0);
}

vec3 applyFog(vec3 color)
{
    float dist = orthographic ? 0.0 : length(cameraPosition - fragPosition);
    float f = 1.0 - exp(-pow(dist / fogDistance, 2.0));
    vec3 horizonDir = normalize(vec3(fragPosition.x - cameraPosition.x, 0.02 * dist, fragPosition.z - cameraPosition.z) + vec3(0.0, 1e-4, 0.0));
    vec3 fogColor = textureLod(environmentMap, horizonDir, 2.0).rgb;
    return mix(color, fogColor, f);
}

// Fraction of the light that reaches this point: 1 = lit, 0 = in shadow.
// Same as lightVisibility() in pbr_frag.glsl; see the comments there.
float lightVisibility(vec3 worldPosition, float NdotL)
{
    if (!useShadows)
        return 1.0;
    vec4 lightSpace = lightSpaceMatrix * vec4(worldPosition, 1.0);
    vec3 p = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
    if (p.z > 1.0)
        return 1.0;
    float slope = clamp(sqrt(1.0 - NdotL * NdotL) / max(NdotL, 0.05), 0.0, 10.0);
    float bias = shadowBias * (1.0 + slope);
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = 0.0;
    for (int x = -pcfRadius; x <= pcfRadius; ++x)
        for (int y = -pcfRadius; y <= pcfRadius; ++y)
            lit += texture(shadowMap, vec3(p.xy + vec2(x, y) * texel, p.z - bias));
    float width = float(2 * pcfRadius + 1);
    lit /= width * width;
    vec2 edge = abs(p.xy * 2.0 - 1.0);
    return mix(lit, 1.0, smoothstep(0.85, 1.0, max(edge.x, edge.y)));
}

vec3 detailNormal(vec3 N)
{
    // Two scrolling layers of the tileable ripple map at different scales, combined with a
    // "whiteout" blend, then rotated into the frame of the Gerstner surface (uv = world xz, so
    // the tangent follows +x and the bitangent +z).
    vec2 uv1 = fragPosition.xz * 0.11 + windDirection * time * 0.035;
    vec2 uv2 = fragPosition.xz * 0.27 + vec2(-windDirection.y, windDirection.x) * time * 0.02;
    vec3 n1 = texture(detailNormalMap, uv1).xyz * 2.0 - 1.0;
    vec3 n2 = texture(detailNormalMap, uv2).xyz * 2.0 - 1.0;
    vec3 tn = normalize(vec3(n1.xy + n2.xy, n1.z * n2.z));
    // Fade the ripples out with distance, where they would only alias.
    float fade = orthographic ? 1.0 : exp(-length(cameraPosition - fragPosition) / 120.0);
    tn.xy *= 0.45 * fade;

    vec3 T = normalize(vec3(1.0, 0.0, 0.0) - N * N.x);
    vec3 B = cross(T, N);
    return normalize(mat3(T, B, N) * tn);
}

void main()
{
    vec3 N = normalize(fragNormal);
    vec3 geometricN = N;
    vec3 L = normalize(lightDirection);
    // The shadow only removes direct light (sun glint and sunlit colour); sky reflections stay.
    float visibility = lightVisibility(fragPosition, max(dot(N, L), 0.0));
    if (useDetailNormals)
        N = detailNormal(N);
    vec3 V = viewVector();
    float NdotV = max(dot(N, V), 0.0);

    // Fresnel (Schlick) with F0 = 0.02 for water: almost no reflection looking straight down,
    // a mirror at grazing angles.
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    vec3 R = reflect(-V, N);
    R.y = abs(R.y); // ripple normals can reflect below the horizon; mirror those back up
    vec3 reflection = useEnvironmentMap ? texture(environmentMap, R).rgb : ambientColor * 1.5;

    // Water body colour: deep blue-green lit by sky and sun, a bit lighter on crests
    // (a cheap stand-in for light scattered inside thin wave tips).
    vec3 deep = vec3(0.002, 0.016, 0.035);
    vec3 scatter = vec3(0.0, 0.09, 0.085);
    float crest = clamp(fragHeight * 0.8 + 0.3, 0.0, 1.0);
    vec3 skyLight = useEnvironmentMap ? textureLod(environmentMap, vec3(0.0, 1.0, 0.0), 6.0).rgb : ambientColor;
    vec3 body = deep * (skyLight * 1.5 + lightColor * 0.25 * visibility) + scatter * crest * max(L.y, 0.0) * lightColor * 0.12 * visibility;

    // Sun glint: GGX specular with a low roughness, in HDR so a bloom pass can pick it up later.
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float a2 = 0.07 * 0.07;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * d * d);
    float specular = D * fresnel * NdotL / max(4.0 * NdotV, 0.1);

    vec3 color = mix(body, reflection, fresnel) + lightColor * specular * visibility;
    // Whitewater is diffuse rather than a sharp mirror reflection. Accumulate its sunlight and
    // ambient skylight before mixing it with the ordinary water colour (and before fog).
    vec3 foamLight = skyLight * 0.85 + lightColor * max(dot(geometricN, L), 0.0) * visibility * 0.85;
    // Localised GGX glint and low diffuse scattering make boat shadows legible on the water.
    // The sky reflection is not shadowed by the lamp.
    if (useSpotlight) {
        vec3 SL;
        float attenuation = spotAttenuation(spotPosition - fragPosition, SL);
        float spotNdotL = max(dot(N, SL), 0.0);
        if (attenuation > 0.0 && spotNdotL > 0.0) {
            vec3 SH = normalize(SL + V);
            float nh = max(dot(N, SH), 0.0);
            float dd = nh * nh * (a2 - 1.0) + 1.0;
            float Dspot = a2 / (PI * dd * dd);
            float specSpot = Dspot * fresnel * spotNdotL / max(4.0 * NdotV, 0.1);
            vec3 scatterSpot = vec3(0.025, 0.085, 0.09) * spotNdotL;
            float shadow = spotVisibility(fragPosition, abs(dot(geometricN, SL)));
            color += spotColor * (attenuation * shadow) * (scatterSpot + vec3(specSpot));
            foamLight += spotColor * (attenuation * shadow) * spotNdotL * 0.85;
        }
    }
    if (useFoam && fragFoam > 0.0) {
        // Reuse the existing tileable water normal map for a fine, irregular coverage mask.
        // World-space UVs keep the pattern fixed when the camera-following grid snaps.
        // Two rotated/scrolled scales avoid a repeated checkerboard or uniform white strips.
        vec2 uv = fragPosition.xz;
        vec2 drift = windDirection * time * 0.012;
        float n1 = texture(detailNormalMap, uv * 0.19 + drift).r;
        float n2 = texture(detailNormalMap, uv.yx * 0.31 - drift * 0.7).g;
        float breakup = smoothstep(0.34, 0.66, 0.55 * n1 + 0.45 * n2);
        float foam = fragFoam * breakup;
        vec3 foamColor = vec3(0.88, 0.94, 0.93) * foamLight;
        color = mix(color, foamColor, foam);
    }
    if (useFog)
        color = applyFog(color);
    fragColor = vec4(color, 1.0);
}
