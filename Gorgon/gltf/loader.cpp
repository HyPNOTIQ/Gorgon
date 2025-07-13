#include "loader.h"
#include <shaders/shared.inl>

namespace gltf
{

Loader::Loader(const CreateInfo& info)
	: device(info.device)
	, vma(info.vma)
	, transferCommandBuffer(info.transferCommandBuffer)
	, transferQueue(info.transferQueue)
	, pipelineLayout(createPipelineLayout(info.device))
	, shaderModule(createShaderModule(info.device))
	, surfaceFormat(info.surfaceFormat)
{
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

		// NORMAL
		if (info.normal)
		{
			addDescription(1, vk::Format::eR32G32B32Sfloat);
		}

#if 0
		// TANGENT
		if (info.data.tangent)
		{
			addDescription(2, vk::Format::eR32G32B32A32Sfloat);
		}

		// TEXCOORD_0
		if (info.data.texcoord_0 != PrimitivePipelineInfo::eTexcoord::None)
		{
			addDescription(3, vk::Format::eR32G32Sfloat); // TODO: handle format
		}

		// TEXCOORD_1
		if (info.data.texcoord_1 != PrimitivePipelineInfo::eTexcoord::None)
		{
			addDescription(4, vk::Format::eR32G32Sfloat); // TODO: handle format
		}

		// COLOR3_0
		if (info.data.color_0 != PrimitivePipelineInfo::eColor::None)
		{
			addDescription(5, vk::Format::eR32G32B32Sfloat); // TODO: handle format
		}
#endif // 0

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
			.polygonMode = vk::PolygonMode::eLine,
			.cullMode = vk::CullModeFlagBits::eBack,
			.lineWidth = 1.0f,
		};

		// Multisample state
		const auto multisampleState = vk::PipelineMultisampleStateCreateInfo{
			.rasterizationSamples = vk::SampleCountFlagBits::e1,
			.minSampleShading = 1.0f,
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

		// Stages
		const auto stages = {
			vk::PipelineShaderStageCreateInfo{
				.stage = vk::ShaderStageFlagBits::eFragment,
				.module = *shaderModule,
				.pName = "main",
			},
			vk::PipelineShaderStageCreateInfo{ // TODO: why two stages is needed? try merge into one
				.stage = vk::ShaderStageFlagBits::eVertex,
				.module = *shaderModule,
				.pName = "main",
			},
		};

		const auto pipelineRendering = vk::PipelineRenderingCreateInfo{}.setColorAttachmentFormats(surfaceFormat);

		const auto createInfo = vk::GraphicsPipelineCreateInfo{
			.pNext = &pipelineRendering,
			.pVertexInputState = &vertexInputState,
			.pInputAssemblyState = &inputAssemblyState,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizationState,
			.pMultisampleState = &multisampleState,
			.pColorBlendState = &colorBlendState,
			.pDynamicState = &dynamicState,
			.layout = *pipelineLayout,
		}.setStages(stages);

		return device.createGraphicsPipeline(nullptr, createInfo);
	};

	const auto it = pipelines.try_emplace(info.packed, createPipeline()).first;
	const auto& result = it->second;
	return *result;
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

vk::raii::PipelineLayout Loader::createPipelineLayout(const vk::raii::Device& device)
{
	const auto pushConstantRange = vk::PushConstantRange{
		.stageFlags = vk::ShaderStageFlagBits::eVertex,
		.offset = 0,
		.size = sizeof(PushConstants),
	};

	const auto createInfo = vk::PipelineLayoutCreateInfo{}.setPushConstantRanges(pushConstantRange);

	return device.createPipelineLayout(createInfo);
}

}