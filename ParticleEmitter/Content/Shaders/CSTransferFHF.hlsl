//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
const float g_densityCoef;

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWTexture3D<uint>	g_rwDensity;
RWTexture3D<float>	g_rwVelocity[3];
RWTexture3D<float4>	g_rwGrid;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	const float density = g_rwDensity[DTid] / 1000.0;
	
	float3 velocity;
	velocity.x = g_rwVelocity[0][DTid];
	velocity.y = g_rwVelocity[1][DTid];
	velocity.z = g_rwVelocity[2][DTid];

	g_rwGrid[DTid] = float4(velocity, density);
	g_rwDensity[DTid] = 0;
	g_rwVelocity[0][DTid] = 0.0;
	g_rwVelocity[1][DTid] = 0.0;
	g_rwVelocity[2][DTid] = 0.0;
}
