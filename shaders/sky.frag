#version 450

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform Scene
{
    layout(offset = 128) mat4 inverseViewProjection;
    layout(offset = 192) vec4 cameraTime;
    layout(offset = 208) vec4 sunTurbidity;
    layout(offset = 224) vec4 skyA;
    layout(offset = 240) vec4 skyB;
    layout(offset = 256) vec4 skyC;
    layout(offset = 272) vec4 skyD;
    layout(offset = 288) vec4 skyE;
    layout(offset = 304) vec4 zenith;
    layout(offset = 320) vec4 zeroThetaSun;
    layout(offset = 336) vec4 rendering;
    layout(offset = 432) vec4 celestial;
} scene;

const float PI = 3.14159265358979323846;

float saturatedDot(vec3 a, vec3 b)
{
    return clamp(dot(a, b), 0.0, 1.0);
}

vec3 perez(float theta, float gamma)
{
    return (vec3(1.0) + scene.skyA.xyz *
            exp(scene.skyB.xyz / max(cos(theta), 0.01))) *
           (vec3(1.0) + scene.skyC.xyz * exp(scene.skyD.xyz * gamma) +
            scene.skyE.xyz * cos(gamma) * cos(gamma));
}

vec3 yxyToRgb(vec3 value)
{
    float safeY = max(value.z, 0.0001);
    vec3 xyz = vec3(
        value.y * value.x / safeY,
        value.x,
        (1.0 - value.y - value.z) * value.x / safeY);
    mat3 transform = mat3(
         2.3706743, -0.5138850,  0.0052982,
        -0.9000405,  1.4253036, -0.0146949,
        -0.4706338,  0.0885814,  1.0093968);
    return max(transform * xyz, vec3(0.0));
}

float starHash(vec2 point)
{
    return fract(sin(dot(point, vec2(127.1, 311.7))) * 43758.5453);
}

float moonHash(vec2 point)
{
    return fract(sin(dot(point, vec2(41.73, 289.11))) * 951.1357);
}

float moonNoise(vec2 point)
{
    vec2 cell = floor(point);
    vec2 local = fract(point);
    local = local * local * (3.0 - 2.0 * local);
    return mix(
        mix(moonHash(cell), moonHash(cell + vec2(1.0, 0.0)), local.x),
        mix(moonHash(cell + vec2(0.0, 1.0)),
            moonHash(cell + vec2(1.0, 1.0)), local.x),
        local.y);
}

vec3 moonRadiance(vec3 direction, vec3 moon, vec3 sun)
{
    const float angularRadius = 0.022;
    float radiusScale = sin(angularRadius);
    vec3 reference = abs(moon.y) > 0.92
        ? vec3(1.0, 0.0, 0.0)
        : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(reference, moon));
    vec3 up = normalize(cross(moon, right));
    vec2 diskCoordinate = vec2(
        dot(direction, right), dot(direction, up)) / radiusScale;
    float radius = length(diskCoordinate);
    float disk = 1.0 - smoothstep(0.965, 1.015, radius);
    if (disk <= 0.0) {
        float alignment = max(dot(direction, moon), 0.0);
        return vec3(0.34, 0.40, 0.52) * pow(alignment, 850.0) * 0.035;
    }

    float surfaceDepth = sqrt(max(1.0 - radius * radius, 0.0));
    vec3 surfaceNormal = normalize(
        right * diskCoordinate.x +
        up * diskCoordinate.y -
        moon * surfaceDepth);
    vec3 lightDirection = normalize(sun + right * 0.10);
    float illumination = max(dot(surfaceNormal, lightDirection), 0.0);
    float limb = smoothstep(0.0, 0.38, surfaceDepth);

    vec2 surface = diskCoordinate * 3.1;
    float maria = moonNoise(surface + vec2(7.3, 2.1)) * 0.62 +
        moonNoise(surface * 2.3 - vec2(4.1, 8.7)) * 0.38;
    float albedo = mix(0.48, 0.92, smoothstep(0.28, 0.78, maria));
    for (int crater = 0; crater < 9; ++crater) {
        float seed = float(crater);
        vec2 center = vec2(
            moonHash(vec2(seed, 3.7)),
            moonHash(vec2(seed, 9.2))) * 1.55 - 0.775;
        float craterRadius = mix(
            0.055, 0.19, moonHash(vec2(seed, 15.4)));
        float distanceToCrater = length(diskCoordinate - center);
        float basin = 1.0 - smoothstep(
            craterRadius * 0.45, craterRadius, distanceToCrater);
        float rim = smoothstep(
            craterRadius * 0.70, craterRadius * 0.90, distanceToCrater) *
            (1.0 - smoothstep(
                craterRadius * 0.90, craterRadius * 1.08,
                distanceToCrater));
        albedo += rim * 0.18 - basin * 0.13;
    }

    vec3 lunarColor = vec3(0.72, 0.73, 0.70) * albedo;
    float earthshine = 0.045 * surfaceDepth;
    return lunarColor * (earthshine + illumination * 0.92) * limb * disk;
}

vec3 skyRadiance(vec3 direction)
{
    vec3 sun = normalize(scene.sunTurbidity.xyz);
    direction.y = max(direction.y, 0.001);
    float theta = acos(saturatedDot(direction, vec3(0.0, 1.0, 0.0)));
    float gamma = acos(saturatedDot(direction, sun));
    vec3 yxy = scene.zenith.xyz * perez(theta, gamma) /
               max(scene.zeroThetaSun.xyz, vec3(0.001));
    vec3 preetham = yxyToRgb(yxy) * 0.045;
    float height = pow(clamp(direction.y, 0.0, 1.0), 0.32);
    vec3 clearSky = mix(
        vec3(0.42, 0.66, 0.88),
        vec3(0.018, 0.13, 0.46),
        height);
    vec3 daySky = mix(clearSky, preetham, 0.24);
    float lowSun =
        (1.0 - smoothstep(0.04, 0.45, abs(sun.y))) *
        smoothstep(-0.32, -0.02, sun.y);
    vec2 horizontalView = normalize(direction.xz);
    vec2 horizontalSun = normalize(sun.xz);
    float towardSun = pow(max(dot(horizontalView, horizontalSun), 0.0), 1.5);
    float horizonBand = exp(-direction.y * 5.2);
    float warmWeight =
        clamp(lowSun * horizonBand * (0.48 + 0.52 * towardSun), 0.0, 0.92);
    vec3 sunsetColor = mix(
        vec3(0.72, 0.12, 0.018),
        vec3(1.05, 0.58, 0.065),
        towardSun);
    daySky = mix(
        daySky * mix(vec3(1.0), vec3(0.90, 0.72, 0.68), lowSun * 0.45),
        sunsetColor,
        warmWeight);
    daySky += vec3(0.34, 0.075, 0.018) *
        lowSun * pow(towardSun, 0.55) * exp(-direction.y * 2.3) * 0.42;
    float sunAlignment = max(dot(direction, sun), 0.0);
    float sunDisk = smoothstep(0.99962, 0.99988, sunAlignment);
    float sunHalo = pow(sunAlignment, 180.0);
    vec3 sunColor = mix(
        vec3(1.0, 0.76, 0.43),
        vec3(1.0, 0.43, 0.09),
        lowSun * 0.72);
    daySky += sunColor * (sunDisk * 2.8 + sunHalo * 0.25);

    vec3 nightSky = mix(
        vec3(0.015, 0.025, 0.065),
        vec3(0.0015, 0.004, 0.018),
        height);
    vec2 starCoordinate = vec2(
        atan(direction.z, direction.x) * 230.0,
        asin(clamp(direction.y, -1.0, 1.0)) * 260.0);
    vec2 starCell = floor(starCoordinate);
    vec2 starLocal = fract(starCoordinate) - 0.5;
    float starSeed = starHash(starCell);
    float twinkle = 0.72 + 0.28 *
        sin(scene.cameraTime.w * 1.7 + starSeed * 31.0);
    float starShape = 1.0 - smoothstep(0.07, 0.19, length(starLocal));
    float stars = smoothstep(0.994, 0.9996, starSeed) * starShape *
        twinkle * smoothstep(0.0, 0.18, direction.y);
    nightSky += vec3(0.72, 0.82, 1.0) * stars * 1.8;

    vec3 moon = normalize(scene.celestial.xyz);
    float moonAlignment = max(dot(direction, moon), 0.0);
    nightSky += moonRadiance(direction, moon, sun);
    nightSky += vec3(0.22, 0.28, 0.42) *
        pow(moonAlignment, 420.0) * 0.035;
    return mix(nightSky, daySky, scene.celestial.w);
}

void main()
{
    vec4 farPoint = scene.inverseViewProjection * vec4(inNdc, 1.0, 1.0);
    vec3 world = farPoint.xyz / farPoint.w;
    vec3 direction = normalize(world - scene.cameraTime.xyz);
    vec3 color = skyRadiance(direction);
    float adaptedExposure =
        scene.rendering.x * mix(2.4, 1.0, scene.celestial.w);
    color = vec3(1.0) - exp(-color * adaptedExposure);
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
