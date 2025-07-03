struct VSOutput {
	float4 position : SV_Position;
	float3 fragColor : COLOR0;
};

static const float2 positions[3] = {
	float2(0.0, -0.5),
	float2(0.5, 0.5),
	float2(-0.5, 0.5)
};

static const float3 colors[3] = {
	float3(1.0, 0.0, 0.0),
	float3(0.0, 1.0, 0.0),
	float3(0.0, 0.0, 1.0)
};

[shader("vertex")]
VSOutput main(uint vertexIndex : SV_VertexID) {
	VSOutput output;
	output.position = float4(positions[vertexIndex], 0.0, 1.0);
	output.fragColor = colors[vertexIndex];
	return output;
}

struct PSInput {
	float3 fragColor : COLOR0;
};

struct PSOutput {
	float4 outColor : SV_Target0;
};

[shader("fragment")]
PSOutput main(PSInput input) {
	PSOutput output;
	output.outColor = float4(input.fragColor, 1.0);
	return output;
}