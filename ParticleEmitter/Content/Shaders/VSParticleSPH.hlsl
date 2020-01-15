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
RWStructuredBuffer<uint> g_rwGrid;
RWStructuredBuffer<uint> g_rwOffsets;
StructuredBuffer<Particle> g_roParticles;

float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_roParticles[ParticleId];

	// Update particle
	const float4 svPos = Update(ParticleId, particle);

	// Build grid
	const uint binIdx = GET_CELL_INDEX(binIdx, particle.Pos, svPos);
	InterlockedAdd(g_rwGrid[binIdx], 1, g_rwOffsets[ParticleId]);

	return svPos;
}
