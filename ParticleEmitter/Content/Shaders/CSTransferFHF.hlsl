//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWTexture3D<uint>	g_rwDensityU;
#if COMPUTE_VISCOSITY
RWTexture3D<float>	g_rwVelocity[3];
RWTexture3D<float4>	g_rwGrid;
#else
RWTexture3D<float>	g_rwDensity;
#endif

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
#if COMPUTE_VISCOSITY
	const float density = asfloat(g_rwDensityU[DTid]);

	float3 velocity;
	velocity.x = g_rwVelocity[0][DTid];
	velocity.y = g_rwVelocity[1][DTid];
	velocity.z = g_rwVelocity[2][DTid];

	g_rwGrid[DTid] = float4(velocity, density);
	g_rwVelocity[0][DTid] = 0.0;
	g_rwVelocity[1][DTid] = 0.0;
	g_rwVelocity[2][DTid] = 0.0;
#else
	g_rwDensity[DTid] = g_rwDensityU[DTid] / 1000.0;
#endif
	g_rwDensityU[DTid] = 0;
}
