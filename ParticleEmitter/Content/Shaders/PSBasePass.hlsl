//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Structs
//--------------------------------------------------------------------------------------
struct PSIn
{
	float4	Pos		: SV_POSITION;
	float3	Norm	: NORMAL;
};

//--------------------------------------------------------------------------------------
// Base geometry-buffer pass
//--------------------------------------------------------------------------------------
min16float4 main(PSIn input) : SV_TARGET
{
	const min16float3 L = min16float3(normalize(float3(1.0.xx, -1.0)));
	const min16float3 N = min16float3(normalize(input.Norm));
	const min16float lightAmt = saturate(dot(N, L));
	const min16float ambient = N.y * 0.5 + 0.5;

	min16float result = lightAmt + ambient;
	result /= result + 0.5;

	return min16float4(result.xxx, 1.0);
}
