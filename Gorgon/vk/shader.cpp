#include "shader.h"

namespace
{

vk::raii::ShaderModule createShaderModule(
	const vk::raii::Device& device,
	const std::filesystem::path& path)
{
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

boost::json::value loadShaderReflection(const std::filesystem::path& path)
{
	auto json_path = path;
	json_path.replace_extension(".json");

	auto file = std::ifstream(json_path);

	if (!file) {
		assert(false);
	}

	return boost::json::parse(file);
}

}

Shader::Shader(const vk::raii::Device& device, const std::filesystem::path& path)
	: module(createShaderModule(device, path))
	, reflection(loadShaderReflection(path))
{}

const vk::raii::ShaderModule& Shader::getModule() const
{
	return module;
}

const boost::json::value& Shader::getReflection() const
{
	return reflection;
}
