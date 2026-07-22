#include "Application/Application.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <exception>

int main(const int argc, char** argv)
{
    try {
        water::Application app;
        if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0) {
            app.runBenchmark();
        } else {
            app.run();
        }
        return 0;
    } catch (const std::exception& error) {
        spdlog::critical("Fatal error: {}", error.what());
        return 1;
    }
}
