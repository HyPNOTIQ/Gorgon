#include "common.inl"

struct PushConstants
{
    float4x4 mvp;
    //float4x4 modelMatrix;
    //float4x4 normalMatrix;
    uint materialIndex;
};

typedef uint PrimitiveFlagsInt;

struct PrimitiveFlags {
    PrimitiveFlagsInt normal : 1;
    PrimitiveFlagsInt tangent : 1;
    PrimitiveFlagsInt texcoord_0 : 1;
    PrimitiveFlagsInt texcoord_1 : 1;
    PrimitiveFlagsInt color_0 : 2; // 1 - vec3, 2 - vec4
    PrimitiveFlagsInt hasBaseColorTexture : 1;
    PrimitiveFlagsInt hasMetallicRoughnessTexture : 1;
    PrimitiveFlagsInt hasNormalTexture : 1;
    PrimitiveFlagsInt hasOcclusionTexture : 1;
    PrimitiveFlagsInt hasEmissiveTexture : 1;
};
