//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define main mainCS
#include "CSEmit.hlsl"
#undef main
#define main mainNoSPH
#include "VSParticle.hlsl"
#undef main

#define C 0
#define L 1
#define R 2
#define U 3
#define D 4
#define F 5
#define B 6

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbSimulation
{
	float	g_smoothRadius;
	float	g_pressureStiffness;
	float	g_restDensity;
	float	g_viscosity;
	float	g_densityCoef;
	float	g_velocityCoef;
};

static const float4 g_boundaryFHF = { BOUNDARY_FHF };
static const float g_cellSize = g_smoothRadius;
static const float g_h_sq = g_smoothRadius * g_smoothRadius;
static const float g_h_cb = g_h_sq * g_smoothRadius;

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWTexture3D<uint> g_rwDensity;
globallycoherent RWTexture3D<float> g_rwVelocity[3];
Texture3D g_roGrid;

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
float CalculateDensity(float r_sq)
{
	// Implements this equation:
	// W_poly6(r, h) = 315 / (64 * pi * h^9) * (h^2 - r^2)^3
	// g_densityCoef = particleMass * 315.0f / (64.0f * PI * g_smoothRadius^9)
	const float d_sq = g_h_sq - r_sq;

	return g_densityCoef * d_sq * d_sq * d_sq;
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

//--------------------------------------------------------------------------------------
// Velocity calculation
//--------------------------------------------------------------------------------------
float3 CalculateVelocity(float r_sq, float3 velocity, float density)
{
	const float r = sqrt(r_sq);
	const float r_cb = r_sq * r;
	// Implements this equation:
	// W_viscosity(r, h) = 15 / (2 * pi * h^3) * (-r^3 / (2 * h^3) + r^2 / h^2 + h / (2 * r) - 1)
	// g_velocityCoef = particleMass * 15 / (2 * pi * g_smoothRadius^3)

	return g_velocityCoef * (-r_cb / (2.0 * g_h_cb) + r_sq / g_h_sq + g_smoothRadius / (2.0 * r) - 1.0) / density;
}

//--------------------------------------------------------------------------------------
// Load grid data
//--------------------------------------------------------------------------------------
void LoadGrid(out float4 gridData[7], float3 tex, float3 texel)
{
	// Load velocities and densities
	gridData[C] = g_roGrid.SampleLevel(g_smpLinear, tex, 0.0);
	gridData[L] = g_roGrid.SampleLevel(g_smpLinear, tex + float3(-0.5, 0.0.xx) * texel, 0.0);
	gridData[R] = g_roGrid.SampleLevel(g_smpLinear, tex + float3(0.5, 0.0.xx) * texel, 0.0);
	gridData[U] = g_roGrid.SampleLevel(g_smpLinear, tex + float3(0.0, -0.5, 0.0) * texel, 0.0);
	gridData[D] = g_roGrid.SampleLevel(g_smpLinear, tex + float3(0.0, 0.5, 0.0) * texel, 0.0);
	gridData[F] = g_roGrid.SampleLevel(g_smpLinear, tex + float3(0.0.xx, -0.5) * texel, 0.0);
	gridData[B] = g_roGrid.SampleLevel(g_smpLinear, tex + float3(0.0.xx, 0.5) * texel, 0.0);
}

//--------------------------------------------------------------------------------------
// Pressure gradient calculation
//--------------------------------------------------------------------------------------
float3 CalculateGradPressure(float4 gridData[7])
{
	// Compute pressures
	const float pressL = CalculatePressure(gridData[L].w);
	const float pressR = CalculatePressure(gridData[R].w);
	const float pressU = CalculatePressure(gridData[U].w);
	const float pressD = CalculatePressure(gridData[D].w);
	const float pressF = CalculatePressure(gridData[F].w);
	const float pressB = CalculatePressure(gridData[B].w);

	const float voxel = 1.0 / g_cellSize;
	float3 deltaPressure;
	deltaPressure.x = pressR - pressL;
	deltaPressure.y = pressU - pressD;
	deltaPressure.z = pressB - pressF;

	return deltaPressure * voxel;
}

//--------------------------------------------------------------------------------------
// Velocity Laplacian calculation
//--------------------------------------------------------------------------------------
float3 CalculateVelocityLaplace(float4 gridData[7])
{
	float3 velocityLaplace = -gridData[C].xyz * 6.0;

	[unroll]
	for (uint i = 1; i < 7; ++i) velocityLaplace += gridData[i].xyz;

	return velocityLaplace;
}

float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_rwParticles[ParticleId];

	// Load densities
	float4 gridData[7];
	const float3 texel = 1.0 / GRID_SIZE_FHF;
	const float3 tex = SimulationToGridTexSpace(particle.Pos);
	LoadGrid(gridData, tex, texel);
	const float3 pressGrad = CalculateGradPressure(gridData);
	const float3 velocityLaplace = CalculateVelocityLaplace(gridData);

	float3 acceleration = -pressGrad;
	acceleration += g_viscosity * velocityLaplace;
	acceleration /= gridData[C].w > 0.0 ? gridData[C].w : 1.0;

	// Update particle
	const float4 svPos = Update(ParticleId, particle, acceleration);

	// Clamp range of cells
	const uint3 cell = SimulationToGridTexSpace(particle.Pos) * GRID_SIZE_FHF;
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
				const float r_sq = dot(disp, disp);
				if (r_sq < g_h_sq)
				{
					const float density = CalculateDensity(r_sq);
#if 1
					const float3 velocity = CalculateVelocity(r_sq, particle.Velocity, gridData[C].w);
					uint rhoEnc = 0xffffffff;
					for (uint k = 0; k < 0xffffffff && rhoEnc == 0xffffffff; ++k)
					{
						InterlockedExchange(g_rwDensity[i], 0xffffffff, rhoEnc);
						DeviceMemoryBarrier();
						if (rhoEnc != 0xffffffff)
						{
							// Critical section
							g_rwDensity[i] = rhoEnc + density * 1000.0;
							if (gridData[C].w > 0.0)
							{
								g_rwVelocity[0][i] += velocity.x;
								g_rwVelocity[1][i] += velocity.y;
								g_rwVelocity[2][i] += velocity.z;
							}
						}
					}
#else
					InterlockedAdd(g_rwDensity[i], density * 1000.0);
#endif
				}
			}
		}
	}

	return svPos;
}
