//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define main mainCS
#include "CSEmit.hlsl"
#undef main
#define main mainNoSPH
#include "VSParticle.hlsl"
#undef main



//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbSimulation
{
	float	g_smoothRadius;
	float	g_pressureStiffness;
	float	g_restDensity;
	float	g_densityCoef;
};

static const float4 g_boundaryFHF = { BOUNDARY_FHF };
static const float g_cellSize = g_smoothRadius;
static const float g_hSq = g_smoothRadius * g_smoothRadius;

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
// Get cell center position
//--------------------------------------------------------------------------------------
float3 GetCellCenterPos(uint3 i, float3 texel)
{
	const float3 tex = (i + 0.5) * texel;
	float3 pos = tex * 2.0 - 1.0;
	pos.y = -pos.y;
	
	return pos * g_boundaryFHF.w + g_boundaryFHF.xyz;;
}

//--------------------------------------------------------------------------------------
// Density calculation
//--------------------------------------------------------------------------------------
float CalculateDensity(float rSq)
{
	// Implements this equation:
	// W_poly6(r, h) = 315 / (64 * pi * h^9) * (h^2 - r^2)^3
	// g_densityCoef = particleMass * 315.0f / (64.0f * PI * g_smoothRadius^9)
	const float dSq = g_hSq - rSq;

	return g_densityCoef * dSq * dSq * dSq;
}

//--------------------------------------------------------------------------------------
// Pressure calculation
//--------------------------------------------------------------------------------------
float CalculatePressure(float density)
{
	// Implements this equation:
	// Pressure = B * ((rho / rho_0)^y - 1)
	const float rhoRatio = density / g_restDensity;

	return g_pressureStiffness * max(rhoRatio * rhoRatio * rhoRatio - 1.0, 0.0);
}

float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_rwParticles[ParticleId];

	// Load densities
	float3 dim;
	g_rwGrid.GetDimensions(dim.x, dim.y, dim.z);
	const float3 texel = 1.0 / dim;
	const float3 tex = SimulationToGridTexSpace(particle.Pos);
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

	const float voxel = 1.0 / g_cellSize;
	float3 pressGrad;
	pressGrad.x = (pressR - pressL) * voxel;
	pressGrad.y = (pressU - pressD) * voxel;
	pressGrad.z = (pressB - pressF) * voxel;

	// Update particle
	const float4 svPos = Update(ParticleId, particle, -pressGrad / density);

	// Clamp range of cells
	const uint3 cell = SimulationToGridTexSpace(particle.Pos) * dim;
	const uint3 startCell = max(cell - 1, 0);
	const uint3 endCell = min(cell + 1, GRID_SIZE_FHF - 1);

	// Calculate the density based on neighbors from the 8 adjacent cells + current cell
	for (uint3 i = startCell; i.z <= endCell.z; ++i.z)
	{
		for (i.y = startCell.y; i.y <= endCell.y; ++i.y)
		{
			for (i.x = startCell.x; i.x <= endCell.x; ++i.x)
			{
				const float3 cellPos = GetCellCenterPos(i, texel);
				const float3 disp = cellPos - particle.Pos;
				const float rSq = dot(disp, disp);
				if (rSq < g_hSq)
				{
					const float density = CalculateDensity(rSq);
					InterlockedAdd(g_rwGrid[i], density * 1000.0);
				}
			}
		}
	}

	return svPos;
}
