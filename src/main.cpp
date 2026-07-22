#include "Application/Application.hpp"

#include <spdlog/spdlog.h>

#include <exception>

int main()
{
    try {
        water::Application app;
        app.run();
        return 0;
    } catch (const std::exception& error) {
        spdlog::critical("Fatal error: {}", error.what());
        return 1;
    }
}
