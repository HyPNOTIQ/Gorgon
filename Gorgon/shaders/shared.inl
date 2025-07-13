#include "common.inl"

struct PushConstants
{
    float4x4 mvp;
};

typedef uint PrimitiveFlagsInt;

struct PrimitiveFlags {
    PrimitiveFlagsInt NORMAL : 1;
    PrimitiveFlagsInt TANGENT : 1;
    PrimitiveFlagsInt TEXCOORD_0 : 1;
    PrimitiveFlagsInt TEXCOORD_1 : 1;
    PrimitiveFlagsInt COLOR_0 : 2;
};
