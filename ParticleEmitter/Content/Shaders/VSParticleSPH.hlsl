//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Common.hlsli"
#define main mainCS
#include "CSEmit.hlsl"
#undef main
#define main mainNoSPH
#include "VSParticle.hlsl"
#undef main

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWStructuredBuffer<uint> g_rwGridParticleCount;
RWStructuredBuffer<uint> g_rwOffsets;
StructuredBuffer<Particle> g_roParticles;

float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_roParticles[ParticleId];

	const float4 svPos = Update(ParticleId, particle);
	const uint3 gsPos = ToGridSpace(particle.Pos);
	if (IsOutOfGrid(gsPos)) return svPos;

	uint offset;
	const uint binIdx = GetGridBinIndex(gsPos);
	InterlockedAdd(g_rwGridParticleCount[binIdx], 1, offset);
	g_rwOffsets[ParticleId] = offset;

	return svPos;
}
