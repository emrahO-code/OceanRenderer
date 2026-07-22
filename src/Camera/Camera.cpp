#include "Camera/Camera.hpp"

#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace water {

glm::vec3 Camera::forward() const
{
    return glm::normalize(glm::vec3(
        std::cos(pitch_) * std::cos(yaw_),
        std::sin(pitch_),
        std::cos(pitch_) * std::sin(yaw_)));
}

glm::vec3 Camera::right() const
{
    return glm::normalize(glm::cross(forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::mat4 Camera::view() const
{
    return glm::lookAt(position_, position_ + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projection(const float aspectRatio) const
{
    auto result = glm::perspective(fieldOfView, aspectRatio, 0.1f, 4000.0f);
    result[1][1] *= -1.0f;
    return result;
}

void Camera::resetMouseTracking()
{
    firstMouse_ = true;
}

void Camera::update(
    GLFWwindow* window, const float deltaSeconds, const bool captureMouse)
{
    if (!captureMouse) {
        resetMouseTracking();
    } else {
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        if (firstMouse_) {
            lastMouseX_ = mouseX;
            lastMouseY_ = mouseY;
            firstMouse_ = false;
        }
        yaw_ += static_cast<float>(mouseX - lastMouseX_) * mouseSensitivity;
        pitch_ -= static_cast<float>(mouseY - lastMouseY_) * mouseSensitivity;
        pitch_ = std::clamp(pitch_, -1.52f, 1.52f);
        lastMouseX_ = mouseX;
        lastMouseY_ = mouseY;
    }

    float speed = movementSpeed_ * deltaSeconds;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 4.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) speed *= 0.25f;

    const auto flatForward = glm::normalize(glm::vec3(forward().x, 0.0f, forward().z));
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position_ += flatForward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position_ -= flatForward * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position_ += right() * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position_ -= right() * speed;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) position_.y += speed;
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) position_.y -= speed;
}

} // namespace water
