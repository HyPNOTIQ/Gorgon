#include "shared.inl"

[[vk::push_constant]]
PushConstants pushConstants;

[SpecializationConstant]
const PrimitiveFlagsInt primitiveFlagsInt = 0;

struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float3 tangent;
    [[vk::location(3)]] float3 texcoord_0;
    [[vk::location(4)]] float3 texcoord_1;
    [[vk::location(5)]] float3 color3_0;
    [[vk::location(6)]] float4 color4_0;
}

struct FSInput {
    float4 color;
}

struct VSOutput {
    float4 position : SV_Position;
    FSInput fs;
}

PrimitiveFlags getPrimitiveFlags(const PrimitiveFlagsInt primitiveFlagsInt)
{
    return reinterpret<PrimitiveFlags>(primitiveFlagsInt);
}

[shader("vertex")]
VSOutput main(const VSInput input) {
	VSOutput output;

    let primitiveFlag = getPrimitiveFlags(primitiveFlagsInt);

    let position = float4(input.position, 1);
    output.position = mul(pushConstants.mvp, position);

    output.fs.color = float4(1, 0, 0, 1);

    if (primitiveFlag.normal == 1)
    {
        output.fs.color = float4(input.normal, 1);
    }

	return output;
}

struct FSOutput {
	float4 outColor : SV_Target0;
}

[shader("fragment")]
FSOutput main(const FSInput input) {
	FSOutput output;

    //let primitiveFlag = getPrimitiveFlags(primitiveFlagsInt);
    output.outColor = input.color;


	return output;
}