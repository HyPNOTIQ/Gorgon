#include <GLFW/glfw3.h>
#include "fmt/ostream.h"
#include <iostream>
#include <VkBootstrap.h>
#include <thread>
#include <atomic>
#include <glm/glm.hpp>
#include <gsl/gsl>

constexpr auto WIDTH = 800u;
constexpr auto HEIGHT = 600u;

void GlfwErrorCallback(
    const int ErrorCode,
    const char* const Description)
{
    fmt::println(std::cerr, "GLFW Error {}: {}", ErrorCode, Description);
}

std::atomic<bool> StopRenderThread(false);

void RenderThreadFunc()
{
    while (!StopRenderThread.load())
    {
        fmt::println("Render Thread");
    }
}

int main() {
    glfwSetErrorCallback(GlfwErrorCallback);

    if (glfwInit() != GLFW_TRUE)
    {
        fmt::println(std::cerr, "Failed to initialize GLFW");
        return EXIT_FAILURE;
    }

    auto GlfwGuard = gsl::finally([] { glfwTerminate(); });

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* const Window = glfwCreateWindow(WIDTH, HEIGHT, "Gorgon", nullptr, nullptr);
    if (!Window) {
        fmt::println(std::cerr, "Failed to create GLFW window");
        return EXIT_FAILURE;
    }

    auto GlfwWindowGuard = gsl::finally([Window] { glfwDestroyWindow(Window); });

    std::jthread RenderThread(RenderThreadFunc);

    while (!glfwWindowShouldClose(Window))
    {
        fmt::println("Main Thread");
        glfwPollEvents();
    }

    StopRenderThread.store(true);

    return EXIT_SUCCESS;
}
