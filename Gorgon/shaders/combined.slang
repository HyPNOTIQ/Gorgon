#include "pushconstants.inl"

[[vk::push_constant]]
PushConstants pushConstants;

struct VSInput {
    float3 inPosition : ATTR0;
}

struct VSOutput {
	float4 position : SV_Position;
	float3 fragColor : COLOR0;
}

static const float3 positions[3] = {
	float3(0.0, 0.0, 0.0),
	float3(1.0, 0.0, 0.0),
	float3(0.0, 1.0, 0.0)
};

[shader("vertex")]
VSOutput main(const VSInput input, const uint vertexIndex: SV_VertexID) {
	VSOutput output;

	let position = float4(input.inPosition, 1.0);
	output.position = mul(pushConstants.mvp, position);
    // output.position = position;
    output.fragColor = input.inPosition;
	return output;
}

struct PSInput {
	float3 fragColor : COLOR0;
}

struct PSOutput {
	float4 outColor : SV_Target0;
}

[shader("fragment")]
PSOutput main(PSInput input) {
	PSOutput output;
	output.outColor = float4(input.fragColor, 1.0);
	return output;
}