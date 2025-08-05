#pragma once
#include "model.h"
#include <vk/vma.h>
#include "vk/shader.h"

namespace gltf
{

struct PrimitivePipelineInfo
{
	bool hasNormal;
	bool hasTangent;
	std::optional<vk::Format> texcoord0;
	std::optional<vk::Format> texcoord1;
	std::optional<vk::Format> color0;
	bool hasBaseColorTexture;
	bool hasMetallicRoughnessTexture;
	bool hasNormalTexture;
	bool hasOcclusionTexture;
	bool hasEmissiveTexture;

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
	vk::Sampler getSampler(const vk::SamplerCreateInfo& info);
    std::unordered_map<vk::SamplerCreateInfo, vk::raii::Sampler> samplers;

	vk::Pipeline getPipeline(const PrimitivePipelineInfo& info);
    std::unordered_map<PrimitivePipelineInfo, vk::raii::Pipeline> pipelines;

	std::vector<Buffer> loadBuffers(const std::vector<std::span<const std::byte>>& buffers);
	Buffer createMaterialsSSBO(const std::vector<Material>& materials);

	struct ImageInfo {
		std::span<const std::byte> imageBuffer;
		vk::Format format;
		vk::Extent3D extent;

		bool operator==(const ImageInfo& rh) const
		{
			return imageBuffer.data() == rh.imageBuffer.data()
				&& imageBuffer.size() == rh.imageBuffer.size()
				&& format == rh.format
				&& extent == rh.extent;
		}
	};

	std::vector<ImageData> createImages(const std::vector<ImageInfo>& imageInfos);

	const vk::raii::Device& device;
	const VulkanMemoryAllocator& vma;
	const vk::raii::CommandBuffer& transferCommandBuffer;
	const vk::raii::Queue& transferQueue; 
	const vk::raii::DescriptorPool descriptorPool;
	const vk::raii::DescriptorPool bindlessDescriptorPool;
	Shader shader;
	vk::Format surfaceFormat;
	vk::Format depthFormat;

	struct PipelineLayoutData {
		vk::raii::PipelineLayout pipelineLayout;
		vk::raii::DescriptorSetLayout descriptorSetLayout;
		vk::raii::DescriptorSetLayout bindlessDescriptorSetLayout;
	} pipelineLayoutData;

	static PipelineLayoutData createPipelineLayoutData(const vk::raii::Device& device);
	//static vk::raii::DescriptorPool createBindlessDescriptorPool(const vk::raii::Device& device);
};

}
