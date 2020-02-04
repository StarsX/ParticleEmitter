//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------
RWTexture3D<float4>	g_rwDst;
Texture3D			g_txSrc;

//--------------------------------------------------------------------------------------
// Texture sampler
//--------------------------------------------------------------------------------------
SamplerState		g_smpLinear;

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float3 dim;
	g_rwDst.GetDimensions(dim.x, dim.y, dim.z);
	const float3 tex = (DTid + 0.5) / dim;

	g_rwDst[DTid] = g_txSrc.SampleLevel(g_smpLinear, tex, 0);
}
