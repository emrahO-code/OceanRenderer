#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace water {

struct OceanSettings {
    std::uint32_t resolution = 256;
    float patchLength = 400.0f;
    glm::vec2 windDirection{1.0f, 0.35f};
    float windSpeed = 24.0f;
    float phillipsAmplitude = 3.0e-7f;
    float smallWaveDamping = 0.12f;
    float choppiness = -1.15f;
    float heightScale = .7f;
    float animationPeriod = 180.0f;
};

struct RenderSettings {
    float sunAzimuth = -1.57079632679f;
    float sunElevation = 0.055f;
    float timeOfDay = 18.0f;
    float dayLengthMinutes = 4.0f;
    float turbidity = 2.1f;
    float exposure = 1.15f;
    float waterDepth = 80.0f;
    float specularPower = 180.0f;
    float specularIntensity = 1.8f;
    float skyIntensity = 0.8f;
    glm::vec3 terrainColor{0.42f, 0.39f, 0.25f};
    glm::vec3 absorption{0.420f, 0.063f, 0.019f};
    glm::vec3 scattering{0.032f, 0.037f, 0.042f};
    glm::vec3 backscattering{0.00065f, 0.00074f, 0.00083f};
    glm::vec3 foamColor{0.88f, 0.94f, 0.96f};
    float foamThreshold = 0.6f;
    float foamIntensity = 1.0f;
    int tileRadius = 3;
    bool automaticDayCycle = false;
    bool foamEnabled = true;
    bool lodEnabled = true;
    bool paused = false;
};

} // namespace water
