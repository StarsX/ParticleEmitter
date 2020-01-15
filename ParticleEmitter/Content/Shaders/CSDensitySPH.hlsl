//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Common.hlsli"

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
cbuffer cbSimulation
{
	uint	g_numParticles;
	float	g_timeStep;
	float	g_smoothRadius;
	float	g_pressureStiffness;
	float	g_restDensity;
	float	g_densityCoef;
	float	g_gradPressureCoef;
	float	g_lapViscosityCoef;
	float	g_wallStiffness;
};

//--------------------------------------------------------------------------------------
// Constants
//--------------------------------------------------------------------------------------
static const float g_hSq = g_smoothRadius * g_smoothRadius;
static const uint g_numCells = GRID_SIZE * GRID_SIZE * GRID_SIZE;

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWStructuredBuffer<float>	g_rwDensities;
StructuredBuffer<Particle>	g_roParticles;
StructuredBuffer<uint>		g_roGrid;

//--------------------------------------------------------------------------------------
// Density calculation
//--------------------------------------------------------------------------------------
float CalculateDensity(float rSq)
{
	// Implements this equation:
	// W_poly6(r, h) = 315 / (64 * pi * h^9) * (h^2 - r^2)^3
	// g_fDensityCoef = fParticleMass * 315.0f / (64.0f * PI * fSmoothlen^9)
	const float dSq = g_hSq - rSq;

	return g_densityCoef * dSq * dSq * dSq;
}

[numthreads(64, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	// Load particle position
	const float3 pos = g_roParticles[DTid].Pos;

	// Get cell position
	const int3 cellPos = ToGridSpace(pos);
	if (IsOutOfGrid(cellPos)) return;

	// Clamp range of cells
	const uint3 startCell = max(cellPos - 1, 0);
	const uint3 endCell = min(cellPos + 1, GRID_SIZE - 1);

	// Calculate the density based on neighbors from the 8 adjacent cells + current cell
	float density = 0.0;
	for (uint3 i = startCell; i.z <= endCell.z; ++i.z)
	{
		for (i.y = startCell.y; i.y <= endCell.y; ++i.y)
		{
			for (i.x = startCell.x; i.x <= endCell.x; ++i.x)
			{
				const uint cellIdx = GridGetCellIndex(i);
				const uint nextCell = cellIdx + 1;
				const uint start = g_roGrid[cellIdx];
				const uint end = nextCell < g_numCells ? g_roGrid[nextCell] : g_numParticles;
				for (uint j = start; j < end; ++j)
				{
					float3 adjPos = g_roParticles[j].Pos;

					const float3 disp = adjPos - pos;
					const float rSq = dot(disp, disp);
					if (rSq < g_hSq) density += CalculateDensity(rSq);
				}
			}
		}
	}

	g_rwDensities[DTid] = density;
}
