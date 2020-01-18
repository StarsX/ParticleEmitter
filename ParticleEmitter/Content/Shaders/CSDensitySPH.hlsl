//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "CSSimulation.hlsli"

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWBuffer<float>	g_rwDensities;

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

[numthreads(64, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	// Load particle position
	const float3 pos = g_roParticles[DTid].Pos;

	// Get cell position
	const int3 cellPos = SimulationToGridSpace(pos);
	if (IsOutOfGrid(cellPos)) return;

	float density = 0.0;

	// Clamp range of cells
	const uint3 startCell = max(cellPos - 1, 0);
	const uint3 endCell = min(cellPos + 1, GRID_SIZE - 1);

	// Calculate the density based on neighbors from the 8 adjacent cells + current cell
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
					const float3 adjPos = g_roParticles[j].Pos;

					const float3 disp = adjPos - pos;
					const float rSq = dot(disp, disp);
					if (rSq < g_hSq) density += CalculateDensity(rSq);
				}
			}
		}
	}

	g_rwDensities[DTid] = density;
}
