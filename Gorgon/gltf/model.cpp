#include "model.h"
#include <shaders/shared.inl>

namespace gltf
{

// TODO: handle no POSITION case 
void Primitive::Draw(const DrawInfo& info) const
{
	const auto& commandBuffer = info.commandBuffer;

	const auto pushConstantsInfo = vk::PushConstantsInfo{
		.layout = info.pipelineLayout,
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		.offset = offsetof(PushConstants, materialIndex),
		.size = sizeof(PushConstants::materialIndex),
		.pValues = &materialIndex,
	};

	commandBuffer.pushConstants2(pushConstantsInfo);

	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

	// dynamic states
	commandBuffer.setPrimitiveTopology(topology);
	commandBuffer.setFrontFace(info.frontFace);

	{
		const auto& surfaceExtent = info.surfaceExtent;

		// https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/
		const auto viewport = vk::Viewport{
			.y = float(surfaceExtent.height),
			.width = float(surfaceExtent.width),
			.height = -float(surfaceExtent.height),
			.maxDepth = 1,
		};
		commandBuffer.setViewport(0, viewport);

		const auto scissor = vk::Rect2D{ .extent = surfaceExtent };
		commandBuffer.setScissor(0, scissor);
	}

	commandBuffer.bindVertexBuffers2(
		0,
		vertexBindData.buffers,
		vertexBindData.offsets,
		vertexBindData.sizes,
		vertexBindData.strides
	);

	(this->*drawFunc)(info);
}

void Primitive::DrawIndexed(const DrawInfo& info) const
{
	assert(indexedData);

	const auto& commandBuffer = info.commandBuffer;

	commandBuffer.bindIndexBuffer2(
		indexedData->buffer,
		indexedData->offset,
		indexedData->size,
		indexedData->type
	);

	commandBuffer.drawIndexed(count, 1, 0, 0, 0);
}

void Primitive::DrawNonIndexed(const DrawInfo& info) const
{
	info.commandBuffer.draw(count, 1, 0, 0 );
}

void Primitive::VertexBindData::add(
	const vk::Buffer buffer,
	const vk::DeviceSize offset,
	const vk::DeviceSize size,
	const vk::DeviceSize stride)
{
	buffers.emplace_back(buffer);
	offsets.emplace_back(offset);
	sizes.emplace_back(size);
	strides.emplace_back(stride);
}

void Mesh::Draw(const DrawInfo& info) const
{
	const auto primitiveDrawInfo = Primitive::DrawInfo{
		.commandBuffer = info.commandBuffer,
		.surfaceExtent = info.surfaceExtent,
		.pipelineLayout = info.pipelineLayout,
	};

	std::ranges::for_each(primitives, [&](const Primitive& primitive) { primitive.Draw(primitiveDrawInfo); });
}

void Node::Draw(const DrawInfo& info) const
{
	if (this->mesh)
	{
		const auto mvp = info.viewProj * modelMatix;

		const auto pushConstantsInfo = vk::PushConstantsInfo{
			.layout = info.pipelineLayout,
			.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
			.offset = offsetof(PushConstants, mvp),
			.size = sizeof(PushConstants::mvp),
			.pValues = &mvp,
		};

		info.commandBuffer.pushConstants2(pushConstantsInfo);

		const auto& mesh = this->mesh->get();

		const auto meshDrawInfo = Mesh::DrawInfo{
			.commandBuffer = info.commandBuffer,
			.surfaceExtent = info.surfaceExtent,
			.pipelineLayout = info.pipelineLayout,
		};

		mesh.Draw(meshDrawInfo);
	}

	std::ranges::for_each(children, [&](const Node& child) { child.Draw(info); });
}

void Scene::Draw(const DrawInfo& info) const
{
	const auto nodeDrawInfo = Node::DrawInfo{
		.viewProj = info.viewProj,
		.commandBuffer = info.commandBuffer,
		.surfaceExtent = info.surfaceExtent,
		.pipelineLayout = info.pipelineLayout,
	};

	std::ranges::for_each(nodes, [&](const Node& node) { node.Draw(nodeDrawInfo); });
}

void Model::Draw(const DrawInfo& info) const
{
	const auto bindDescriptorSetsInfo = vk::BindDescriptorSetsInfo{
		.stageFlags = vk::ShaderStageFlagBits::eFragment,
		.layout = data.pipelineLayout,
	}.setDescriptorSets(data.descriptorSets);

	info.commandBuffer.bindDescriptorSets2(bindDescriptorSetsInfo);

	const auto sceneIndex = info.sceneIndex;
	const auto& scenes = data.scenes;

	assert(scenes.size() > sceneIndex);

	const auto& scene = scenes[sceneIndex];
	
	const auto sceneDrawInfo = Scene::DrawInfo{
		.viewProj = info.viewProj,
		.commandBuffer = info.commandBuffer,
		.surfaceExtent = info.surfaceExtent,
		.pipelineLayout = data.pipelineLayout,
	};

	scene.Draw(sceneDrawInfo);
}

Model::~Model()
{
	//(*data.device).freeDescriptorSets(data.descriptorPool, data.descriptorSets);
}

}