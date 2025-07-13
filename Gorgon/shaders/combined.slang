#include "shared.inl"

[[vk::push_constant]]
PushConstants pushConstants;

[SpecializationConstant]
const PrimitiveFlagsInt primitiveFlagsInt = 0;

struct VSInput {
    float3 position : ATTR0;
    float3 normal : ATTR1;
    float3 tangent : ATTR2;
    float3 texcoord_0 : ATTR3;
    float3 texcoord_1 : ATTR4;
    float3 color3_0 : ATTR5;
    float4 color4_0 : ATTR6;
}

struct FSInput {
    float4 color : COLOR0;
}

struct VSOutput {
    float4 position : SV_Position;
    FSInput fs;
}

[shader("vertex")]
VSOutput main(const VSInput input) {
	VSOutput output;

    let primitiveFlag = reinterpret<PrimitiveFlags>(primitiveFlagsInt);

    let position = float4(input.position, 1);
    output.position = mul(pushConstants.mvp, position);

    if (primitiveFlag.COLOR_0 == 1)
    {
        output.fs.color = float4(input.color3_0, 1);
    }
    else if (primitiveFlag.COLOR_0 == 2)
    {
        output.fs.color = input.color4_0;
    }

	return output;
}

struct FSOutput {
	float4 outColor : SV_Target0;
}

[shader("fragment")]
FSOutput main(const FSInput input) {
	FSOutput output;

    let primitiveFlag = reinterpret<PrimitiveFlags>(primitiveFlagsInt);

    if (primitiveFlag.COLOR_0 != 0)
    {
        output.outColor = input.color;
    }
    else
    {
        output.outColor = float4(1, 0, 0, 1);
    }

	return output;
}