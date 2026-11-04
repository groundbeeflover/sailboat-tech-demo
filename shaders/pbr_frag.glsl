#version 410

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec4 fragTangent;

uniform vec3 cameraPosition;
uniform bool orthographic;
uniform vec3 viewForward;

uniform vec3 lightDirection; // towards the light
uniform vec3 lightColor;
uniform vec3 ambientColor; // used when the environment map is off

uniform samplerCube environmentMap;
uniform bool useEnvironmentMap;
uniform float environmentMaxLod;

uniform bool usePBR;
uniform bool useFog;
uniform float fogDistance;

uniform bool useShadows;
uniform sampler2DShadow shadowMap;
uniform sampler2DShadow shadowMapWide;
uniform mat4 lightSpaceMatrixWide;
uniform bool useCascades;
uniform float cascadeSplit;
uniform float cascadeOverlap;
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


// Material
uniform vec3 albedoFactor;
uniform float roughnessFactor;
uniform float metallicFactor;
uniform vec3 emissive;
uniform bool doubleSided;
uniform float transmission; // zero for all materials except thin sailcloth
uniform bool useSsao;
uniform sampler2D ssaoTexture;
uniform vec2 ssaoViewport;
uniform vec2 uvScale;
uniform vec2 uvOffset; // e.g. the current frame of a flipbook atlas
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D roughnessMap;
uniform bool hasAlbedoMap;
uniform bool hasNormalMap;
uniform bool hasRoughnessMap;

out vec4 fragColor;

const float PI = 3.14159265;

// ---- Cook-Torrance terms (Karis 2013)
float distributionGGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0; // remapping for analytic lights
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Analytic fit of the split-sum BRDF integration LUT (Karis 2014, "Physically Based Shading on Mobile").
vec3 environmentBRDF(vec3 F0, float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

vec3 viewVector()
{
    return orthographic ? -viewForward : normalize(cameraPosition - fragPosition);
}

// Fraction of the light that reaches this point: 1 = lit, 0 = in shadow.
// Keep in sync with the copy in water_frag.glsl (GLSL 4.1 has no #include).
float shadowSample(sampler2DShadow source, vec3 p, float NdotL)
{
    if (p.z <= 0.0 || p.z >= 1.0 || any(lessThan(p.xy, vec2(0.0))) || any(greaterThan(p.xy, vec2(1.0))))
        return 1.0;
    float slope = clamp(sqrt(max(1.0 - NdotL * NdotL, 0.0)) / max(NdotL, 0.05), 0.0, 10.0);
    float bias = shadowBias * (1.0 + slope);
    vec2 texel = 1.0 / vec2(textureSize(source, 0));
    float lit = 0.0;
    for (int x = -pcfRadius; x <= pcfRadius; ++x)
        for (int y = -pcfRadius; y <= pcfRadius; ++y)
            lit += texture(source, vec3(p.xy + vec2(x, y) * texel, p.z - bias));
    float width = float(2 * pcfRadius + 1);
    return lit / (width * width);
}

// The near map retains fine boat details; the wide map covers the island and course.
// A soft transition inside the near-map border prevents a visible cascade seam.
// Perspective uses a frustum-depth split; top-down orthographic uses spatial coverage.
float lightVisibility(vec3 worldPosition, float NdotL)
{
    if (!useShadows)
        return 1.0;
    vec4 nearClip = lightSpaceMatrix * vec4(worldPosition, 1.0);
    vec3 nearP = nearClip.xyz / nearClip.w * 0.5 + 0.5;
    float nearLit = shadowSample(shadowMap, nearP, NdotL);
    vec2 nearEdge = abs(nearP.xy * 2.0 - 1.0);
    if (!useCascades)
        return mix(nearLit, 1.0, smoothstep(0.85, 1.0, max(nearEdge.x, nearEdge.y)));
    vec4 farClip = lightSpaceMatrixWide * vec4(worldPosition, 1.0);
    vec3 farP = farClip.xyz / farClip.w * 0.5 + 0.5;
    float farLit = shadowSample(shadowMapWide, farP, NdotL);
    vec2 farEdge = abs(farP.xy * 2.0 - 1.0);
    farLit = mix(farLit, 1.0, smoothstep(0.94, 1.0, max(farEdge.x, farEdge.y)));
    // Camera-frustum cascades for perspective cameras; the overlap makes the change gradual.
    // The orthographic map has no meaningful camera-depth split, so it uses world coverage.
    float transition = orthographic
        ? smoothstep(0.72, 0.93, max(nearEdge.x, nearEdge.y))
        : smoothstep(cascadeSplit - cascadeOverlap, cascadeSplit + cascadeOverlap,
            dot(worldPosition - cameraPosition, viewForward));
    return mix(nearLit, farLit, transition);
}


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
    return mix(color, textureLod(environmentMap, horizonDir, 2.0).rgb, f);
}

void main()
{
    vec2 uv = fragTexCoord * uvScale + uvOffset;
    vec3 N = normalize(fragNormal);
    vec3 V = viewVector();
    if (doubleSided && dot(N, V) < 0.0)
        N = -N; // thin surfaces (sail): light the side we are looking at
    vec3 geometricN = N;
    float ambientOcclusion = useSsao ? texture(ssaoTexture, gl_FragCoord.xy / ssaoViewport).r : 1.0;
    // Shadow lookup with the geometric normal: the bias must follow the real surface, not the normal map.
    float visibility = lightVisibility(fragPosition, abs(dot(N, normalize(lightDirection))));

    if (hasNormalMap) {
        // TBN: re-orthogonalise the interpolated tangent against the normal (Gram-Schmidt).
        vec3 T = normalize(fragTangent.xyz - N * dot(N, fragTangent.xyz));
        vec3 B = cross(N, T) * fragTangent.w;
        vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
        N = normalize(mat3(T, B, N) * tangentNormal);
    }

    vec3 albedo = albedoFactor * (hasAlbedoMap ? texture(albedoMap, uv).rgb : vec3(1.0));
    float roughness = clamp(roughnessFactor * (hasRoughnessMap ? texture(roughnessMap, uv).r : 1.0), 0.04, 1.0);
    float metallic = metallicFactor;

    vec3 L = normalize(lightDirection);
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    vec3 R = reflect(-V, N);

    vec3 color;
    if (usePBR) {
        // Metals have no diffuse term and tint their reflection; dielectrics reflect ~4%.
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlick(VdotH, F0);
        float D = distributionGGX(NdotH, roughness * roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3 specular = D * G * F / (4.0 * NdotV * max(NdotL, 1e-4));
        vec3 kD = (1.0 - F) * (1.0 - metallic);
        color = (kD * albedo / PI + specular) * lightColor * NdotL * visibility;

        // Image-based ambient light from the sky cubemap: the blurriest mip approximates irradiance,
        // and the mip level chosen by roughness approximates the prefiltered radiance (split sum).
        if (useEnvironmentMap) {
            vec3 irradiance = textureLod(environmentMap, N, environmentMaxLod).rgb;
            vec3 prefiltered = textureLod(environmentMap, R, roughness * (environmentMaxLod - 1.0)).rgb;
            vec3 Fa = fresnelSchlick(NdotV, F0);
            vec3 kDa = (1.0 - Fa) * (1.0 - metallic);
            color += (kDa * albedo * irradiance + prefiltered * environmentBRDF(F0, roughness, NdotV)) * ambientOcclusion;
        } else {
            color += (albedo * ambientColor * (1.0 - metallic) + F0 * ambientColor) * ambientOcclusion;
        }
    } else {
        // Blinn-Phong reference (for side-by-side comparisons), shininess derived from roughness.
        float shininess = 2.0 / max(pow(roughness, 4.0), 1e-4) - 2.0;
        vec3 diffuse = albedo * (1.0 - metallic) * NdotL;
        vec3 specular = mix(vec3(0.04), albedo, metallic) * pow(NdotH, shininess) * (shininess + 8.0) / (8.0 * PI) * NdotL;
        color = (diffuse + specular) * lightColor * visibility + albedo * ambientColor * ambientOcclusion;
        if (useEnvironmentMap)
            color += mix(vec3(0.04), albedo, metallic) * textureLod(environmentMap, R, roughness * (environmentMaxLod - 1.0)).rgb * ambientOcclusion;
    }

    // Thin sailcloth transmits diffuse light through its *away-from-camera* side.
    // Keep this separate from reflection BRDF and direct shadows; no refraction/alpha transparency.
    if (transmission > 0.0)
        color += albedo * lightColor * transmission * max(dot(-geometricN, L), 0.0) * visibility;

    // Same direct BRDF as the directional sun, but using the lamp's local direction and intensity.
    // Only this direct contribution is shadowed: emission and environment light remain unaffected.
    if (useSpotlight) {
        vec3 SL;
        float attenuation = spotAttenuation(spotPosition - fragPosition, SL);
        float spotNdotL = max(dot(N, SL), 0.0);
        if (attenuation > 0.0) {
            vec3 radiance = spotColor * attenuation * spotVisibility(fragPosition, abs(dot(geometricN, SL)));
            // Backlit sailcloth still transmits when the front-facing BRDF has NdotL = 0.
            if (transmission > 0.0)
                color += albedo * radiance * transmission * max(dot(-geometricN, SL), 0.0);
            if (spotNdotL > 0.0) {
            vec3 SH = normalize(SL + V);
            float spotNdotH = max(dot(N, SH), 0.0);
            float spotVdotH = max(dot(V, SH), 0.0);
            vec3 F0 = mix(vec3(0.04), albedo, metallic);
            if (usePBR) {
                vec3 F = fresnelSchlick(spotVdotH, F0);
                float D = distributionGGX(spotNdotH, roughness * roughness);
                float G = geometrySmith(NdotV, spotNdotL, roughness);
                vec3 specular = D * G * F / (4.0 * NdotV * max(spotNdotL, 1e-4));
                vec3 kD = (1.0 - F) * (1.0 - metallic);
                color += (kD * albedo / PI + specular) * radiance * spotNdotL;
            } else {
                float shininess = 2.0 / max(pow(roughness, 4.0), 1e-4) - 2.0;
                vec3 diffuse = albedo * (1.0 - metallic) * spotNdotL;
                vec3 specular = F0 * pow(spotNdotH, shininess) * (shininess + 8.0) / (8.0 * PI) * spotNdotL;
                color += (diffuse + specular) * radiance;
            }
            }
        }
    }

    color += emissive;
    if (useFog)
        color = applyFog(color);
    fragColor = vec4(color, 1.0);
}
