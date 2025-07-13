#pragma once
#include "model.h"
#include <vk/vma.h>

namespace gltf
{

enum class TexcoordType : uint8_t {
	None = 0,
	Float2 = 1,
	Half2 = 2,
	Uint16_2 = 3
};

union PrimitivePipelineInfo
{
	struct {
		size_t normal : 1;
		size_t tangent : 1;
		size_t texcoord_0 : 2;
		size_t texcoord_1 : 2;
		size_t color_0 : 3;
	} data;

	size_t packed = 0;

	static_assert(sizeof(packed) >= sizeof(data));
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
		vk::Format surfaceFormat;
		vk::Format depthFormat;
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
	vk::Format depthFormat;

	static vk::raii::ShaderModule createShaderModule(const vk::raii::Device& device);
	static vk::raii::PipelineLayout createPipelineLayout(const vk::raii::Device& device);
};

}