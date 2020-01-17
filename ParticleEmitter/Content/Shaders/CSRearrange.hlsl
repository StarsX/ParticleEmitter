//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Common.hlsli"

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWStructuredBuffer<Particle> g_rwParticles;
StructuredBuffer<Particle> g_roParticles;
Buffer<uint> g_roGrid;
Buffer<uint> g_roOffsets;

[numthreads(64, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	// Load particle
	const Particle particle = g_roParticles[DTid];

	// Get bin index and particle Id
	const uint cellIdx = GridGetCellIndexWithPosition(particle.Pos);
	const uint particleId = g_roGrid[cellIdx] + g_roOffsets[DTid];

	//g_rwParticles[particleId] = particle;
	g_rwParticles[DTid] = particle;
}
