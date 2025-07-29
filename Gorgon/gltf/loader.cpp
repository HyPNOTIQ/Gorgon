#include "loader.h"
#include <shaders/shared.inl>

namespace gltf
{

Loader::Loader(const CreateInfo& info)
	: device(info.device)
	, vma(info.vma)
	, transferCommandBuffer(info.transferCommandBuffer)
	, transferQueue(info.transferQueue)
	, pipelineLayoutData(createPipelineLayoutData(info.device))
	, shaderModule(createShaderModule(info.device))
	, surfaceFormat(info.surfaceFormat)
	, depthFormat(info.depthFormat)
{}

vk::Sampler Loader::getSampler(const vk::SamplerCreateInfo& info)
{
	auto it = samplers.find(info);
	if (it == samplers.end()) {
		const auto& [new_it, inserted] = samplers.emplace(info, device.createSampler(info));
		assert(inserted);

		return new_it->second;
	}
	return it->second;
}

vk::Pipeline Loader::getPipeline(const PrimitivePipelineInfo& info)
{
	const auto createPipeline = [&]
	{
		auto bindingDescriptions = vku::small::vector<vk::VertexInputBindingDescription, VERTEX_INPUT_NUM>();
		auto attributeDescriptions = vku::small::vector<vk::VertexInputAttributeDescription, VERTEX_INPUT_NUM>();

		auto addDescription = [&, bindingIndex = uint32_t(0)](const uint32_t location, const vk::Format format) mutable {
			const auto bindingDescription = vk::VertexInputBindingDescription{
				.binding = bindingIndex++,
				.inputRate = vk::VertexInputRate::eVertex,
			};

			const auto attributeDescription = vk::VertexInputAttributeDescription{
				.location = location,
				.binding = bindingDescription.binding,
				.format = format,
			};

			bindingDescriptions.emplace_back(bindingDescription);
			attributeDescriptions.emplace_back(attributeDescription);
			};

		// POSITION
		addDescription(0, vk::Format::eR32G32B32Sfloat);

		union {
			PrimitiveFlags data;
			PrimitiveFlagsInt packed = 0;

			static_assert(sizeof(packed) >= sizeof(data));
		} primitiveFlags;

		// NORMAL

		if (info.hasNormal)
		{
			addDescription(1, vk::Format::eR32G32B32Sfloat);
			primitiveFlags.data.normal = 1;
		}

		// TANGENT
		if (info.hasTangent)
		{
			addDescription(2, vk::Format::eR32G32B32A32Sfloat);
			primitiveFlags.data.tangent = 1;
		}

		const auto checkTexcoordFormat = [](const vk::Format val) {
			const auto formats = {
				vk::Format::eR32G32Sfloat,
				vk::Format::eR8G8Unorm,
				vk::Format::eR16G16Unorm,
			};

			for (const auto format : formats) {
				if (format == val)
				{
					return true;
				}
			}

			return false;
		};

		// TEXCOORD_0
		if (info.texcoord0)
		{
			const auto format = info.texcoord0.value();

			assert(checkTexcoordFormat(format));

			addDescription(3, format);
			primitiveFlags.data.texcoord_0 = 1;
		}

		// TEXCOORD_1
		if (info.texcoord1)
		{
			const auto format = info.texcoord1.value();

			assert(checkTexcoordFormat(format));

			addDescription(4, format);
			primitiveFlags.data.texcoord_1 = 1;
		}

		// COLOR3_0
		if (info.color0)
		{
			const auto format = info.color0.value();

			switch (format) {
			case vk::Format::eR32G32B32Sfloat:
			case vk::Format::eR8G8B8Unorm:
			case vk::Format::eR16G16B16Unorm: 
				addDescription(5, format);
				primitiveFlags.data.color_0 = 1;
				break;
			case vk::Format::eR32G32B32A32Sfloat:
			case vk::Format::eR8G8B8A8Unorm:
			case vk::Format::eR16G16B16A16Unorm:
				addDescription(6, format);
				primitiveFlags.data.color_0 = 2;
				break;
			default: assert(false);
			}
		}

		const auto vertexInputState = vk::PipelineVertexInputStateCreateInfo{}
			.setVertexBindingDescriptions(bindingDescriptions)
			.setVertexAttributeDescriptions(attributeDescriptions);

		// Input assembly
		const auto inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
			.topology = vk::PrimitiveTopology::eTriangleList,
			.primitiveRestartEnable = false
		};

		// Viewport state
		const auto viewportState = vk::PipelineViewportStateCreateInfo{
			.viewportCount = 1,
			.scissorCount = 1,
		};

		// Rasterization State
		const auto rasterizationState = vk::PipelineRasterizationStateCreateInfo{
			.polygonMode = vk::PolygonMode::eFill,
			.cullMode = vk::CullModeFlagBits::eBack,
			.lineWidth = 1.0f,
		};

		// Multisample state
		const auto multisampleState = vk::PipelineMultisampleStateCreateInfo{
			.rasterizationSamples = vk::SampleCountFlagBits::e1,
			.minSampleShading = 1.0f,
		};

		// Depth
		const auto depthStencilState = vk::PipelineDepthStencilStateCreateInfo{
			.depthTestEnable = true,
			.depthWriteEnable = true,
			.depthCompareOp = vk::CompareOp::eLess,
		};

		// Color blend state
		const auto colorBlendAttachmentState = vk::PipelineColorBlendAttachmentState{
			.colorWriteMask = ~vk::ColorComponentFlags(),
		};

		const auto colorBlendState = vk::PipelineColorBlendStateCreateInfo{
		}.setAttachments(colorBlendAttachmentState);

		// Dynamic state
		const auto dynamicStates = {
			vk::DynamicState::ePrimitiveTopology,
			vk::DynamicState::eVertexInputBindingStride,
			vk::DynamicState::eFrontFace,
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor,
		};

		const auto dynamicState = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamicStates);

		const auto SpecializationMapEntries = {
			vk::SpecializationMapEntry{
				.constantID = 0,
				.offset = 0,
				.size = sizeof(primitiveFlags.packed),
			},
		};

		const auto specializationInfo = vk::SpecializationInfo{
			.dataSize = sizeof(primitiveFlags.packed),
			.pData = &primitiveFlags.packed,
		}.setMapEntries(SpecializationMapEntries);
		//.setData(primitiveFlags.packed); TODO:

		const auto createPipelineShaderStageCreateInfo = [&](const vk::ShaderStageFlagBits stage) {
			return vk::PipelineShaderStageCreateInfo{
				.stage = stage,
				.module = *shaderModule,
				.pName = "main",
				.pSpecializationInfo = &specializationInfo,
			};
		};

		const auto stages = {
			createPipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment),
			createPipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex),
		};

		const auto pipelineRendering = vk::PipelineRenderingCreateInfo{}
		.setColorAttachmentFormats(surfaceFormat)
		.setDepthAttachmentFormat(depthFormat);

		const auto createInfo = vk::GraphicsPipelineCreateInfo{
			.pNext = &pipelineRendering,
			.pVertexInputState = &vertexInputState,
			.pInputAssemblyState = &inputAssemblyState,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizationState,
			.pMultisampleState = &multisampleState,
			.pDepthStencilState = &depthStencilState,
			.pColorBlendState = &colorBlendState,
			.pDynamicState = &dynamicState,
			.layout = *pipelineLayoutData.pipelineLayout,
		}.setStages(stages);

		return device.createGraphicsPipeline(nullptr, createInfo);
	};

	auto it = pipelines.find(info);
	if (it == pipelines.end()) {
		const auto& [new_it, inserted] = pipelines.emplace(info, createPipeline());
		assert(inserted);

		return new_it->second;
	}
	return it->second;
}

vk::raii::ShaderModule Loader::createShaderModule(const vk::raii::Device& device)
{
	const auto path = std::filesystem::path("shaders/combined.spv");

	const auto loadSPIRV = [](const std::filesystem::path& path) {
		auto file = std::ifstream(path, std::ios::binary | std::ios::ate);

		if (!file) {
			assert(false);
		}

		const auto size = file.tellg();
		file.seekg(0, std::ios::beg);

		if (size % sizeof(uint32_t) != 0) {
			assert(false);
		}

		std::vector<uint32_t> buffer(size / sizeof(uint32_t));
		if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
			assert(false);
		}

		constexpr auto SPIRV_MAGIC = static_cast<uint32_t>(0x07230203);
		if (buffer.empty() || buffer[0] != SPIRV_MAGIC) {
			assert(false && "Invalid SPIR-V magic number.");
		}

		return buffer;
	};

	const auto code = loadSPIRV(path);

	const auto createInfo = vk::ShaderModuleCreateInfo{}.setCode(code);

	return device.createShaderModule(createInfo);
}

Loader::PipelineLayoutData Loader::createPipelineLayoutData(const vk::raii::Device& device)
{
	auto descriptorPool = [&] {
		const auto poolSize = vk::DescriptorPoolSize{
			.type = vk::DescriptorType::eCombinedImageSampler,
			.descriptorCount = 256u, // TODO
		};

		const auto createInfo = vk::DescriptorPoolCreateInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
			.maxSets = 256u, // TODO
			.poolSizeCount = 256u, // TODO
		}.setPoolSizes(poolSize);

		return device.createDescriptorPool(createInfo);
	}();

	auto descriptorSetLayout = [&] {
		const auto descriptorSetLayoutBindings = {
			vk::DescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 256, // TODO
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
		};

		const auto flags = vk::DescriptorBindingFlagBits::ePartiallyBound
			| vk::DescriptorBindingFlagBits::eVariableDescriptorCount
			| vk::DescriptorBindingFlagBits::eUpdateAfterBind;

		const auto flagsCreateInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo{}.setBindingFlags(flags);

		const auto descriptorSetLayoutCreateInfo = vk::DescriptorSetLayoutCreateInfo{
			.pNext = &flagsCreateInfo,
			.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
		}.setBindings(descriptorSetLayoutBindings);

		return device.createDescriptorSetLayout(descriptorSetLayoutCreateInfo);
	}();

	constexpr auto pushConstantRange = vk::PushConstantRange{
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		.size = sizeof(PushConstants),
	};

	const auto createInfo = vk::PipelineLayoutCreateInfo{}
		.setSetLayouts(*descriptorSetLayout)
		.setPushConstantRanges(pushConstantRange);

	return PipelineLayoutData{
		.pipelineLayout = device.createPipelineLayout(createInfo),
		.descriptorPool = std::move(descriptorPool),
		.descriptorSetLayout = std::move(descriptorSetLayout),
	};
}

}