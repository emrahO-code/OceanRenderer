#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace water {

class Camera {
public:
    Camera() = default;

    void update(GLFWwindow* window, float deltaSeconds, bool captureMouse);

    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspectRatio) const;
    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] const glm::vec3& position() const { return position_; }
    [[nodiscard]] float speed() const { return movementSpeed_; }

    void setSpeed(float speed) { movementSpeed_ = speed; }

private:
    void resetMouseTracking();

    static constexpr float fieldOfView = 1.0471975512f;
    static constexpr float mouseSensitivity = 0.0022f;

    glm::vec3 position_{0.0f, 100.0f, 18.0f};
    float yaw_{-1.57079632679f};
    float pitch_{-0.12f};
    float movementSpeed_{22.0f};
    bool firstMouse_{true};
    double lastMouseX_{};
    double lastMouseY_{};
};

} // namespace water
