#pragma once
#include "model.h"
#include <vk/vma.h>

namespace gltf
{

union PrimitivePipelineInfo
{
	struct {
		bool normal : 1;
		//size_t hasTangent : 1;
		//size_t texcoordType : 2; // 0 = none, 1�3 = valid
		//size_t colorType : 3; // 0 = none, 1�6 = valid
	};

	size_t packed = 0;
};

class Loader
{
public:
	struct CreateInfo
	{
		const vk::raii::Device& device;
		const VulkanMemoryAllocator& vma;
		const vk::raii::CommandBuffer& transferCommandBuffer;
		const vk::raii::Queue& transferQueue;
		//vk::Extent2D surfaceExtent;
		vk::Format surfaceFormat;
	};

    Model loadFromFile(const std::string_view& gltfFile);

	Loader(const CreateInfo& info);
	Loader(const Loader&) = delete;
	Loader(Loader&&) noexcept = default;

private:
	vk::Pipeline getPipeline(const PrimitivePipelineInfo& info);

    std::unordered_map<decltype(PrimitivePipelineInfo::packed), vk::raii::Pipeline> pipelines;
	const vk::raii::Device& device;
	const VulkanMemoryAllocator& vma;
	const vk::raii::CommandBuffer& transferCommandBuffer;
	const vk::raii::Queue& transferQueue;
	vk::raii::ShaderModule shaderModule;
	vk::raii::PipelineLayout pipelineLayout;
	vk::Format surfaceFormat;

	static vk::raii::ShaderModule createShaderModule(const vk::raii::Device& device);
	static vk::raii::PipelineLayout createPipelineLayout(const vk::raii::Device& device);
};

}