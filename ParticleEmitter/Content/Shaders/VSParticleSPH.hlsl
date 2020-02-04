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
// Buffers
//--------------------------------------------------------------------------------------
RWBuffer<uint>	g_rwGrid;
RWBuffer<uint>	g_rwOffsets;
StructuredBuffer<Particle>	g_roParticles;
Buffer<float3>	g_roForces;

//--------------------------------------------------------------------------------------
// Vertex shader of particle integration or emission for SPH
//--------------------------------------------------------------------------------------
float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_roParticles[ParticleId];
	const float3 acceleration = g_roForces[ParticleId];

	// Update particle
	const float4 svPos = UpdateParticleForVS(ParticleId, particle, acceleration);

	// Build grid
	const uint cellIdx = GridGetCellIndexWithPosition(particle.Pos);
	InterlockedAdd(g_rwGrid[cellIdx], 1, g_rwOffsets[ParticleId]);

	return svPos;
}
