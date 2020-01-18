//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "CSSimulation.hlsli"

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWBuffer<float3> g_rwForces;
Buffer<float>	g_roDensities;

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

//--------------------------------------------------------------------------------------
// Pressure gradient calculation
//--------------------------------------------------------------------------------------
float3 CalculateGradPressure(float r, float d, float pressure, float adjPressure, float adjDensity, float3 disp)
{
	const float avgPressure = 0.5 * (adjPressure + pressure);
	// Implements this equation:
	// W_spkiey(r, h) = 15 / (pi * h^6) * (h - r)^3
	// GRAD(W_spikey(r, h)) = -45 / (pi * h^6) * (h - r)^2
	// g_pressureGradCoef = particleMass * -45.0f / (PI * g_smoothRadius^6)

	return g_pressureGradCoef * avgPressure * d * d * disp / (adjDensity * r);
}

//--------------------------------------------------------------------------------------
// Velocity Laplacian calculation
//--------------------------------------------------------------------------------------
float3 CalculateVelocityLaplace(float d, float3 velocity, float3 adjVelocity, float adjDensity)
{
	float3 velDisp = (adjVelocity - velocity);
	// Implements this equation:
	// W_viscosity(r, h) = 15 / (2 * pi * h^3) * (-r^3 / (2 * h^3) + r^2 / h^2 + h / (2 * r) - 1)
	// LAPLACIAN(W_viscosity(r, h)) = 45 / (pi * h^6) * (h - r)
	// g_viscosityLaplaceCoef = particleMass * viscosity * 45.0f / (PI * g_smoothRadius^6)

	return g_viscosityLaplaceCoef * d * velDisp / adjDensity;
}

[numthreads(64, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	// Load particle
	const Particle particle = g_roParticles[DTid];
	const float density = g_roDensities[DTid];

	// Get cell position
	const int3 cellPos = SimulationToGridSpace(particle.Pos);
	if (IsOutOfGrid(cellPos)) return;

	const float pressure = CalculatePressure(density);
	float3 acceleration = 0.0;

	// Clamp range of cells
	const uint3 startCell = max(cellPos - 1, 0);
	const uint3 endCell = min(cellPos + 1, GRID_SIZE_SPH - 1);

	// Calculate the acceleration based on neighbors from the 8 adjacent cells + current cell
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
					const Particle adjParticle = g_roParticles[j];
					
					const float3 disp = adjParticle.Pos - particle.Pos;
					const float rSq = dot(disp, disp);
					if (rSq < g_hSq && j != DTid)
					{
						const float adjDensity = g_roDensities[j];
						const float r = sqrt(rSq);
						const float d = g_smoothRadius - r;
						const float adjPressure = CalculatePressure(adjDensity);

						// Pressure term
						acceleration += CalculateGradPressure(r, d, pressure, adjPressure, adjDensity, disp);

						// Viscosity term
						acceleration += CalculateVelocityLaplace(d, particle.Velocity, adjParticle.Velocity, adjDensity);
					}
				}
			}
		}
	}

	g_rwForces[DTid] = acceleration / density;
}
