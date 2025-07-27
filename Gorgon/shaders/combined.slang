#include "shared.inl"

[[vk::push_constant]]
PushConstants pushConstants;

[SpecializationConstant]
const PrimitiveFlagsInt primitiveFlagsInt = 0;

//[[vk::binding(0, 1)]]
//uniform SamplerState samplers[];
//uniform Texture2D textures[];
uniform Sampler2D textures[];

static const bool HasTexcoord_0 = true;
 
struct VSInput
{
    float3 position;
    float3 normal;
    float3 tangent;
    float2 texcoord_0;
    float2 texcoord_1;
    float3 color3_0;
    float4 color4_0;
}

struct FSInput
{
    float4 color;
    float2 texcoord_0;
    float2 texcoord_1;
}

struct VSOutput
{
    float4 position : SV_Position;
    FSInput fs;
}

PrimitiveFlags getPrimitiveFlags(const PrimitiveFlagsInt primitiveFlagsInt)
{
    return reinterpret<PrimitiveFlags>(primitiveFlagsInt);
}

[shader("vertex")]
VSOutput main(const VSInput input)
{
    VSOutput output;

    let primitiveFlag = getPrimitiveFlags(primitiveFlagsInt);

    let position = float4(input.position, 1);
    output.position = mul(pushConstants.mvp, position);

    output.fs.color = float4(1, 0, 0, 1);

    if (primitiveFlag.texcoord_0 == 1)
    {
        output.fs.texcoord_0 = input.texcoord_0;
    }

    if (primitiveFlag.texcoord_1 == 1)
    {
        output.fs.texcoord_1 = input.texcoord_1;
    }

    //if (primitiveFlag.color_0 == 1)
    //{
    //    output.fs.color = float4(input.color3_0, 1);
    //}
    //else if (primitiveFlag.color_0 == 1)
    //{
    //    output.fs.color = input.color4_0;
    //}

    return output;
}

struct Material
{
    DescriptorHandle<Texture2D> albedo;
    DescriptorHandle<SamplerState> sampler;
}

ParameterBlock<Material> materials[];

struct FSOutput
{
    float4 outColor : SV_Target0;
}

[shader("fragment")]
FSOutput main(const FSInput input, float4 coord: SV_Position)
{
    FSOutput output;

    let primitiveFlag = getPrimitiveFlags(primitiveFlagsInt);

    output.outColor = float4(1);
    let text_index = coord.x > 1440 / 2 ? 2 : 1;
    output.outColor = textures[text_index].Sample(input.texcoord_0);

    //output.outColor = materials[0].albedo.Sample(materials[0].sampler, input.texcoord_0);
    

    return output;
}
