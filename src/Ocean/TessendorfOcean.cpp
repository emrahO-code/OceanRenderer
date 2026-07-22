#include "Ocean/TessendorfOcean.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <stdexcept>

namespace water {
namespace {

constexpr float gravity = 9.81f;

bool isPowerOfTwo(const std::uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

TessendorfOcean::TessendorfOcean(const OceanSettings& settings)
{
    rebuild(settings);
}

void TessendorfOcean::rebuild(const OceanSettings& settings)
{
    if (!isPowerOfTwo(settings.resolution) || settings.resolution < 16) {
        throw std::runtime_error("Ocean resolution must be a power of two >= 16");
    }

    settings_ = settings;
    settings_.windDirection =
        glm::length(settings_.windDirection) > 1.0e-4f
            ? glm::normalize(settings_.windDirection)
            : glm::vec2(1.0f, 0.0f);

    const auto n = settings_.resolution;
    const auto count = static_cast<std::size_t>(n) * n;
    spectralSeeds_.resize(count);

    std::mt19937 randomEngine(0x0CE4A123u);
    std::normal_distribution<float> gaussian(0.0f, 1.0f);
    const float deltaK =
        2.0f * std::numbers::pi_v<float> / settings_.patchLength;
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const auto index = static_cast<std::size_t>(y) * n + x;
            const auto sx =
                static_cast<std::int32_t>(x) -
                static_cast<std::int32_t>(n / 2);
            const auto sy =
                static_cast<std::int32_t>(y) -
                static_cast<std::int32_t>(n / 2);
            const glm::vec2 waveVector =
                glm::vec2(static_cast<float>(sx), static_cast<float>(sy)) *
                deltaK;
            const float length = glm::length(waveVector);
            const std::complex<float> noise(
                gaussian(randomEngine), gaussian(randomEngine));
            const std::complex<float> initialAmplitude =
                noise *
                std::sqrt(
                    std::max(phillipsSpectrum(waveVector), 0.0f) * 0.5f);
            const float baseFrequency =
                2.0f * std::numbers::pi_v<float> /
                settings_.animationPeriod;
            const float angularFrequency =
                length > 1.0e-6f
                    ? std::floor(
                        std::sqrt(gravity * length) / baseFrequency) *
                        baseFrequency
                    : 0.0f;
            spectralSeeds_[index] = glm::vec4(
                initialAmplitude.real(), initialAmplitude.imag(),
                angularFrequency, 0.0f);
        }
    }
    ++revision_;
}

float TessendorfOcean::phillipsSpectrum(
    const glm::vec2& waveVector) const
{
    const float kSquared = glm::dot(waveVector, waveVector);
    if (kSquared < 1.0e-12f) return 0.0f;

    const float kLength = std::sqrt(kSquared);
    const float alignment =
        glm::dot(waveVector / kLength, settings_.windDirection);
    const float largestWave =
        settings_.windSpeed * settings_.windSpeed / gravity;
    const float dampingSquared =
        settings_.smallWaveDamping * settings_.smallWaveDamping;
    const float directional = alignment < 0.0f
        ? 0.07f * alignment * alignment
        : alignment * alignment;

    return settings_.phillipsAmplitude
        * std::exp(-1.0f / (kSquared * largestWave * largestWave))
        * directional
        * std::exp(-kSquared * dampingSquared)
        / (kSquared * kSquared);
}

} // namespace water
