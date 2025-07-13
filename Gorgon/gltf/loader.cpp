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
	, depthFormat(info.depthFormat)
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

		union {
			PrimitiveFlags data;
			PrimitiveFlagsInt packed = 0;

			static_assert(sizeof(packed) >= sizeof(data));
		} primitiveFlags;

		// NORMAL
		if (info.data.normal)
		{
			addDescription(1, vk::Format::eR32G32B32Sfloat);
			primitiveFlags.data.normal = 1;
		}

		// TANGENT
		if (info.data.tangent)
		{
			addDescription(2, vk::Format::eR32G32B32A32Sfloat);
			primitiveFlags.data.tangent = 1;
		}

		// TEXCOORD_0
		if (info.data.texcoord_0)
		{
			const auto format = [&]
			{
				vk::Format result;
				switch (info.data.texcoord_0) {
				case 1: result = vk::Format::eR32G32Sfloat; break;
				case 2: result = vk::Format::eR8G8Unorm; break;
				case 3: result = vk::Format::eR16G16Unorm; break;
				default: assert(false);
				}

				return result;
			}();
			addDescription(3, format); // TODO: handle format
			primitiveFlags.data.texcoord_0 = 1;
		}

		// TEXCOORD_1
		if (info.data.texcoord_1)
		{
			const auto format = [&]
				{
					vk::Format result;
					switch (info.data.texcoord_0) {
					case 1: result = vk::Format::eR32G32Sfloat; break;
					case 2: result = vk::Format::eR8G8Unorm; break;
					case 3: result = vk::Format::eR16G16Unorm; break;
					default: assert(false);
					}

					return result;
			}();

			addDescription(4, format); // TODO: handle format
			primitiveFlags.data.texcoord_1 = 1;
		}

		// COLOR3_0
		if (info.data.color_0)
		{
			const auto format = [&]
			{
				vk::Format result;
				switch (info.data.color_0) {
				case 1: result = vk::Format::eR32G32B32Sfloat; break;
				case 2: result = vk::Format::eR8G8B8A8Unorm; break;
				case 3: result = vk::Format::eR16G16B16A16Unorm; break;
				case 4: result = vk::Format::eR32G32B32A32Sfloat; break;
				case 5: result = vk::Format::eR8G8B8Unorm; break;
				case 6: result = vk::Format::eR16G16B16Unorm; break;
				default: assert(false);
				}

				return result;
			}();
			addDescription(5, format);

			primitiveFlags.data.color_0 = 1;
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
		//.setData(primitiveFlags.packed);

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
	constexpr auto pushConstantRange = vk::PushConstantRange{
		.stageFlags = vk::ShaderStageFlagBits::eVertex,
		.size = sizeof(PushConstants),
	};

	const auto createInfo = vk::PipelineLayoutCreateInfo{}.setPushConstantRanges(pushConstantRange);

	return device.createPipelineLayout(createInfo);
}

}