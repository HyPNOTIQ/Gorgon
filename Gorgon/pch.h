#pragma once

// std
#include <iostream>
#include <thread>
#include <filesystem>
#include <atomic>
#include <unordered_map>
#include <ranges>

// third party
#include <slang/slang.h>
#include <gsl/gsl>
#include <CLI/CLI.hpp>
#include <fmt/ostream.h>
#include <tiny_gltf.h>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/utility/vk_small_containers.hpp>
#include <vma/vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>
#include <boost/pfr.hpp>

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

constexpr auto COLOR_SUBRESOURCE_RANGE = vk::ImageSubresourceRange{
	.aspectMask = vk::ImageAspectFlagBits::eColor,
	.levelCount = 1,
	.layerCount = 1,
};

// Pipe operator for side-effect lambdas (no return value)
template <typename T, typename Func>
	requires std::regular_invocable<Func, T>&& std::is_void_v<std::invoke_result_t<Func, T>>
void operator|(const std::optional<T>& opt, Func&& func) {
	if (opt) {
		std::invoke(std::forward<Func>(func), *opt);
	}
}

namespace std {
    template <typename T1, typename T2>
    struct hash<std::pair<T1, T2>> {
        size_t operator()(const std::pair<T1, T2>& p) const noexcept {
            //return boost::pfr::hash_value(p);
            size_t h1 = std::hash<T1>{}(p.first);
            size_t h2 = std::hash<T2>{}(p.second);
            return h1 ^ (h2 << 1); // simple combination
        } // TODO: return boost::pfr::hash_fields(val);
    };
}