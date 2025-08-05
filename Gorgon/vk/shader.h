#pragma once

class Shader
{
public:
	Shader(
		const vk::raii::Device& device,
		const std::filesystem::path& path
	);

	const vk::raii::ShaderModule& getModule() const;
	const boost::json::value& getReflection() const;

private:
	vk::raii::ShaderModule module;
	boost::json::value reflection;
};