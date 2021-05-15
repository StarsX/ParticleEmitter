//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Structs
//--------------------------------------------------------------------------------------
struct VSIn
{
	float3	Pos	: POSITION;
	float3	Nrm	: NORMAL;
};

struct VSOut
{
	float4	Pos		: SV_POSITION;
	float3	Norm	: NORMAL;
};

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbPerObject
{
	matrix	g_worldViewProj;
	matrix	g_worldViewProjPrev;
	float3x3 g_world;
};

//--------------------------------------------------------------------------------------
// Base geometry pass
//--------------------------------------------------------------------------------------
VSOut main(VSIn input)
{
	VSOut output;

	const float4 pos = { input.Pos, 1.0 };
	output.Pos = mul(pos, g_worldViewProj);
	output.Norm = mul(input.Nrm, g_world);

	return output;
}
