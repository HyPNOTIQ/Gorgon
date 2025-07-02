#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <fmt/ostream.h>
#include <iostream>
#include <thread>
#include <filesystem>
#include <atomic>
#include <ranges>
#include <gsl/gsl>

#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/utility/vk_small_containers.hpp>