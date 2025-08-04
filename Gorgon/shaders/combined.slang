#include "shared.inl"

[[vk::push_constant]]
PushConstants pushConstants;

[SpecializationConstant]
const PrimitiveFlagsInt primitiveFlagsInt = 0;

static const uint TEXCOORD_NUM = 2;

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
    float2 texcoord[TEXCOORD_NUM];
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
        output.fs.texcoord[0] = input.texcoord_0;
    }

    if (primitiveFlag.texcoord_1 == 1)
    {
        output.fs.texcoord[1] = input.texcoord_1;
    }

    // if (primitiveFlag.color_0 == 1)
    //{
    //     output.fs.color = float4(input.color3_0, 1);
    // }
    // else if (primitiveFlag.color_0 == 1)
    //{
    //     output.fs.color = input.color4_0;
    // }

    return output;
}

struct Texture
{
    DescriptorHandle<Sampler2D> texture;
    //DescriptorHandle<SamplerState> samplerState;
    uint uv;

    float4 Sample(const float2 texcoord[TEXCOORD_NUM])
    {
        //return texture.Sample(samplerState, texcoord[uv]);
        return texture.Sample(texcoord[uv]);
    }
}

struct Material
{
    float4 baseColorFactor;
    Texture baseColorTexture;
    Texture metallicRoughnessTexture;
    Texture normalTexture;
    Texture occlusionTexture;
    Texture emissiveTexture;
    float metallicFactor;
    float roughnessFactor;
    float normalTextureScale;
    float alphaCutoff;
}

StructuredBuffer<Material> materials;

struct FSOutput  
{
    float4 outColor : SV_Target0;
}

//layout(set = 1, binding = 1) uniform Sampler2D textures[];

[shader("fragment")]
FSOutput main(const FSInput input, float4 coord: SV_Position)
{
    FSOutput output;

    output.outColor = float4(1);

    let primitiveFlag = getPrimitiveFlags(primitiveFlagsInt);

    let material = materials[pushConstants.materialIndex];

    var baseColor = material.baseColorFactor;

    if (primitiveFlag.hasBaseColorTexture == 1)
    {
        baseColor *= material.baseColorTexture.Sample(input.texcoord);
    }

    baseColor = material.metallicRoughnessTexture.Sample(input.texcoord);

    //baseColor = textures[2].Sample(input.texcoord[0]);

    output.outColor = baseColor;

    return output;
}
