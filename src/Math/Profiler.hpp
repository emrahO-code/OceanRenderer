#pragma once

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace water {

class Profiler {
public:
    struct Sample {
        std::string name;
        double smoothedMilliseconds{};
    };

    void record(const char* name, double milliseconds)
    {
        auto& entry = samples_[name];
        entry.name = name;
        entry.smoothedMilliseconds =
            entry.smoothedMilliseconds == 0.0
                ? milliseconds
                : entry.smoothedMilliseconds * 0.92 + milliseconds * 0.08;
    }

    [[nodiscard]] std::vector<Sample> samples() const
    {
        std::vector<Sample> result;
        result.reserve(samples_.size());
        for (const auto& [_, sample] : samples_) result.push_back(sample);
        std::ranges::sort(result, {}, &Sample::smoothedMilliseconds);
        std::ranges::reverse(result);
        return result;
    }

private:
    std::unordered_map<std::string, Sample> samples_;
};

class ProfileScope {
public:
    ProfileScope(Profiler& profiler, const char* name)
        : profiler_(profiler), name_(name), start_(Clock::now()) {}

    ~ProfileScope()
    {
        const auto elapsed =
            std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
        profiler_.record(name_, elapsed);
    }

private:
    using Clock = std::chrono::steady_clock;
    Profiler& profiler_;
    const char* const name_;
    const Clock::time_point start_;
};

} // namespace water
