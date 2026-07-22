#pragma once

#include "Renderer/RenderSettings.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace water {

class TessendorfOcean {
public:
    explicit TessendorfOcean(const OceanSettings& settings);

    TessendorfOcean(const TessendorfOcean&) = delete;
    TessendorfOcean& operator=(const TessendorfOcean&) = delete;

    void rebuild(const OceanSettings& settings);

    [[nodiscard]] const OceanSettings& settings() const { return settings_; }
    [[nodiscard]] const std::vector<glm::vec4>& spectralSeeds() const
    {
        return spectralSeeds_;
    }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
    [[nodiscard]] float phillipsSpectrum(const glm::vec2& waveVector) const;

    OceanSettings settings_{};
    std::vector<glm::vec4> spectralSeeds_;
    std::uint64_t revision_{};
};

} // namespace water
