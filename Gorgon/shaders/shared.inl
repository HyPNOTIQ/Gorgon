#include "common.inl"

struct PushConstants
{
    float4x4 mvp;
};

typedef uint PrimitiveFlagsInt;

struct PrimitiveFlags {
    PrimitiveFlagsInt normal : 1;
    PrimitiveFlagsInt tangent : 1;
    PrimitiveFlagsInt texcoord_0 : 1;
    PrimitiveFlagsInt texcoord_1 : 1;
    PrimitiveFlagsInt color_0 : 2;
};