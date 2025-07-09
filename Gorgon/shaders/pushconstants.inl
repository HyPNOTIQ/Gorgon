#include "common.inl"

struct PushConstants
{
    float4x4 viewProjection; // 4x4 matrix
};

struct Flags {
    int r : 1;
    int g : 1;
    int b : 1;
};