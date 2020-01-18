//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define main mainCS
#include "CSEmit.hlsl"
#undef main
#define main mainNoSPH
#include "VSParticle.hlsl"
#undef main

static float4 g_boundaryFHF = { BOUNDARY_FHF };

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWTexture3D<uint> g_rwGrid;
Texture3D<float> g_roDensity;

//--------------------------------------------------------------------------------------
// Sampler
//--------------------------------------------------------------------------------------
SamplerState g_smpLinear;

//--------------------------------------------------------------------------------------
// Transform from simulation space to grid texture space
//--------------------------------------------------------------------------------------
float3 SimulationToGridTexSpace(float3 v)
{
	v = (v - g_boundaryFHF.xyz) / g_boundaryFHF.w; // [-1, 1]
	v.y = -v.y;

	return v * 0.5 + 0.5;
}

//--------------------------------------------------------------------------------------
// Pressure calculation
//--------------------------------------------------------------------------------------
float CalculatePressure(float density)
{
	// Implements this equation:
	// Pressure = B * ((rho / rho_0)^y - 1)
	const float rhoRatio = density / 1000.0f;

	return 200.0f * max(rhoRatio * rhoRatio * rhoRatio - 1.0, 0.0);
}

float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_rwParticles[ParticleId];

	// Load densities
	float3 dim;
	g_rwGrid.GetDimensions(dim.x, dim.y, dim.z);
	const float3 texel = 1.0 / dim;
	float3 tex = SimulationToGridTexSpace(particle.Pos);
	const float density = g_roDensity.SampleLevel(g_smpLinear, tex, 0.0);
	const float densityL = g_roDensity.SampleLevel(g_smpLinear, tex + float3(-0.5, 0.0.xx) * texel, 0.0);
	const float densityR = g_roDensity.SampleLevel(g_smpLinear, tex + float3(0.5, 0.0.xx) * texel, 0.0);
	const float densityU = g_roDensity.SampleLevel(g_smpLinear, tex + float3(0.0, -0.5, 0.0) * texel, 0.0);
	const float densityD = g_roDensity.SampleLevel(g_smpLinear, tex + float3(0.0, 0.5, 0.0) * texel, 0.0);
	const float densityF = g_roDensity.SampleLevel(g_smpLinear, tex + float3(0.0.xx, -0.5) * texel, 0.0);
	const float densityB = g_roDensity.SampleLevel(g_smpLinear, tex + float3(0.0.xx, 0.5) * texel, 0.0);

	// Compute pressures
	const float pressL = CalculatePressure(densityL);
	const float pressR = CalculatePressure(densityR);
	const float pressU = CalculatePressure(densityU);
	const float pressD = CalculatePressure(densityD);
	const float pressF = CalculatePressure(densityF);
	const float pressB = CalculatePressure(densityB);

	const float voxel = 1.0 / (g_boundaryFHF.w * 2.0 / GRID_SIZE_FHF);
	float3 pressGrad;
	pressGrad.x = (pressR - pressL) * voxel;
	pressGrad.y = (pressU - pressD) * voxel;
	pressGrad.z = (pressB - pressF) * voxel;

	// Update particle
	const float4 svPos = Update(ParticleId, particle, -pressGrad / density);

	// Build grid
	tex = SimulationToGridTexSpace(particle.Pos);
	if (any(tex < 0.0) || any(tex > 1.0)) return svPos;
	InterlockedAdd(g_rwGrid[tex * dim], 1);

	return svPos;
}
