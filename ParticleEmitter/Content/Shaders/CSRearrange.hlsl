//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Common.hlsli"

//--------------------------------------------------------------------------------------
// Structs
//--------------------------------------------------------------------------------------
struct Particle
{
	float3 Pos;
	float3 Velocity;
	float LifeTime;
};

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWStructuredBuffer<Particle> g_rwParticles;
StructuredBuffer<Particle> g_roParticles;
StructuredBuffer<uint> g_roBinAddress;
StructuredBuffer<uint> g_roOffsets;

[numthreads(64, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	// Load particle
	Particle particle = g_roParticles[DTid];
	const uint3 gsPos = ToGridSpace(particle.Pos);
	if (IsOutOfGrid(gsPos)) return;

	// Get bin index
	const uint offset = g_roOffsets[DTid];
	const uint binIdx = GetGridBinIndex(gsPos);

	// Get base index of the target location
	const uint baseIdx = g_roBinAddress[binIdx];

	g_rwParticles[baseIdx + offset] = particle;
}
