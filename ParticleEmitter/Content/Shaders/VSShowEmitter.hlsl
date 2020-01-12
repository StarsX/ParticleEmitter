//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Structs
//--------------------------------------------------------------------------------------
struct Emitter
{
	uint3 Indices;
	float2 Barycoord;
};

struct Vertex
{
	float3 Pos;
	float3 Nrm;
};

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbPerObject
{
	matrix g_worldViewProj;
};

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
StructuredBuffer<Emitter>	g_roEmitters;
StructuredBuffer<Vertex>	g_roVertices;

float4 main(uint vId : SV_VERTEXID) : SV_POSITION
{
	const Emitter emitter = g_roEmitters[vId];
	const float3 barycoord = { emitter.Barycoord, 1.0 - (emitter.Barycoord.x + emitter.Barycoord.y) };
	
	float3x3 v;
	[unroll] for (uint i = 0; i < 3; ++i)
		v[i] = g_roVertices[emitter.Indices[i]].Pos;

	const float3 pos = mul(barycoord, v);

	float4 result = mul(float4(pos, 1.0), g_worldViewProj);
	result.z -= 0.001;

	return result;
}
