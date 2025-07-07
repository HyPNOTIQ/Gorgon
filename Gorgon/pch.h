#pragma once

//#include <vulkan/vulkan.h>

#include <fmt/ostream.h>
#include <iostream>
#include <thread>
#include <filesystem>
#include <atomic>
#include <ranges>
#include <gsl/gsl>
#include <CLI/CLI.hpp>
#include <tiny_gltf.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/utility/vk_small_containers.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

//#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

constexpr auto UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();