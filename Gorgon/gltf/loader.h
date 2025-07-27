#pragma once
#include "model.h"
#include <vk/vma.h>

namespace gltf
{

struct PrimitivePipelineInfo
{
	bool hasNormal;
	bool hasTangent;
	std::optional<vk::Format> texcoord0;
	std::optional<vk::Format> texcoord1;
	std::optional<vk::Format> color0;

	auto operator<=>(const PrimitivePipelineInfo&) const = default;
};

}

namespace std {

template<>
struct hash<gltf::PrimitivePipelineInfo> {
	size_t operator()(const gltf::PrimitivePipelineInfo& val) const {
		return boost::pfr::hash_fields(val);
	}
};

}

namespace gltf
{

class Loader
{
public:
	struct CreateInfo
	{
		const vk::raii::Device& device;
		const VulkanMemoryAllocator& vma;
		const vk::raii::CommandBuffer& transferCommandBuffer;
		const vk::raii::Queue& transferQueue;
		vk::Format surfaceFormat;
		vk::Format depthFormat;
	};

    Model loadFromFile(const std::string_view& gltfFile);

	Loader(const CreateInfo& info);
	Loader(const Loader&) = delete;
	Loader(Loader&&) noexcept = default;

private:
	vk::Pipeline getPipeline(const PrimitivePipelineInfo& info);

    std::unordered_map<PrimitivePipelineInfo, vk::raii::Pipeline> pipelines;
	const vk::raii::Device& device;
	const VulkanMemoryAllocator& vma;
	const vk::raii::CommandBuffer& transferCommandBuffer;
	const vk::raii::Queue& transferQueue;
	vk::raii::ShaderModule shaderModule;
	vk::Format surfaceFormat;
	vk::Format depthFormat;

	struct PipelineLayoutData {
		vk::raii::PipelineLayout pipelineLayout;
		vk::raii::DescriptorPool descriptorPool;
		vk::raii::DescriptorSetLayout descriptorSetLayout;
	} pipelineLayoutData;

	static vk::raii::ShaderModule createShaderModule(const vk::raii::Device& device);
	static PipelineLayoutData createPipelineLayoutData(const vk::raii::Device& device);
};

}
