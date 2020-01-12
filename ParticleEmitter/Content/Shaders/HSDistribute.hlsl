//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

// Input control point
struct HSCtrlPointIn
{
	float3	Pos	: POSITION;
	uint	VId	: VERTEXID;
};

// Output control point
struct HSCtrlPointOut
{
	float3	Pos	: POSITION;
	uint	VId	: VERTEXID;
};

// Output patch constant data.
struct HSConstDataOut
{
	float EdgeTessFactor[4]		: SV_TessFactor;
	float InsideTessFactor[2]	: SV_InsideTessFactor;
	float4 MinMaxPt				: AABB;
	float3x3 ToTangentSpace		: TANGENT_SPACE;
};

#define NUM_CONTROL_POINTS 3

// Patch Constant Function
HSConstDataOut CalcHSPatchConstants(
	InputPatch<HSCtrlPointIn, NUM_CONTROL_POINTS> ip,
	uint patchID : SV_PrimitiveID)
{
	HSConstDataOut output;

	// Compute tangent space
	float3 e[2];
	e[0] = ip[2].Pos - ip[1].Pos;
	e[1] = ip[0].Pos - ip[2].Pos;
	float3 u = { 0.0, 1.0, 0.0 };
	float3 r = { 1.0, 0.0.xx };
	const float3 n = normalize(cross(e[0], e[1]));
	if (abs(dot(n, u)) < 0.6667)
	{
		r = cross(n, u);
		u = cross(r, n);
	}
	else
	{
		u = cross(r, n);
		r = cross(n, u);
	}
	output.ToTangentSpace = float3x3(r, u, n);

	// Transform to tangent space
	float3 v[3];
	[unroll] for (uint i = 0; i < 3; ++i)
		v[i] = mul(output.ToTangentSpace, ip[i].Pos);

	// Compute AABB
	output.MinMaxPt.xy = min(v[0].xy, min(v[1].xy, v[2].xy));
	output.MinMaxPt.zw = max(v[0].xy, max(v[1].xy, v[2].xy));
	output.MinMaxPt.xy = floor(output.MinMaxPt.xy);
	output.MinMaxPt.zw = ceil(output.MinMaxPt.zw);
	const float2 aabb = output.MinMaxPt.zw - output.MinMaxPt.xy;

	// Output tess factors
	output.EdgeTessFactor[0] = output.EdgeTessFactor[2] = aabb.y;
	output.EdgeTessFactor[1] = output.EdgeTessFactor[3] = aabb.x;
	output.InsideTessFactor[0] = aabb.x;
	output.InsideTessFactor[1] = aabb.y;

	return output;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("point")]
[outputcontrolpoints(3)]
[patchconstantfunc("CalcHSPatchConstants")]
HSCtrlPointOut main(
	InputPatch<HSCtrlPointIn, NUM_CONTROL_POINTS> ip,
	uint i : SV_OutputControlPointID,
	uint patchID : SV_PrimitiveID)
{
	HSCtrlPointOut output;

	// Insert code to compute Output here
	output.Pos = ip[i].Pos;
	output.VId = ip[i].VId;

	return output;
}
