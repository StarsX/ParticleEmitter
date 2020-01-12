//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define VELOCITY_DECAY	0.999

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
// Constant buffer
//--------------------------------------------------------------------------------------
cbuffer cbPerObject
{
	matrix g_viewProj;
	float g_timeStep;
};

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
RWStructuredBuffer<Particle> g_rwParticles;

float4 main(uint vId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_rwParticles[vId];
	particle.Velocity.y -= 9.8 * g_timeStep;
	particle.Pos += particle.Velocity * g_timeStep;
	particle.Velocity *= VELOCITY_DECAY;
	particle.LifeTime += g_timeStep;

	g_rwParticles[vId] = particle;

	return mul(float4(particle.Pos, 1.0), g_viewProj);
}
