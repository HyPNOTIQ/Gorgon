#pragma once


// std
#include <iostream>
#include <thread>
#include <filesystem>
#include <atomic>
#include <unordered_map>
#include <ranges>

// third party
#include <gsl/gsl>
#include <CLI/CLI.hpp>
#include <fmt/ostream.h>
#include <tiny_gltf.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/utility/vk_small_containers.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>

#if defined(RENDERDOC_INCLUDE) && !defined(NDEBUG)
#include <renderdoc_app.h>
#define USE_RENDER_DOC
#endif

template<typename T>
using OptionalRef = std::optional<std::reference_wrapper<T>>;

#define VULKAN_CONFIGURATOR
constexpr auto VK_API_VERSION = VK_API_VERSION_1_4;
constexpr auto VERTEX_INPUT_NUM = 7u;

constexpr auto UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();
constexpr auto MAT4_IDENTITY = glm::identity<glm::mat4>();
constexpr auto QUAT_IDENTITY = glm::identity<glm::quat>();

