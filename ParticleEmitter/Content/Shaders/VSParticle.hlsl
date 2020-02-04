//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#ifndef main
#define main mainCS
#include "CSEmit.hlsl"
#undef main
#endif

//--------------------------------------------------------------------------------------
// Particle integration or emission
//--------------------------------------------------------------------------------------
void UpdateParticle(uint particleId, inout Particle particle, float3 acceleration)
{
	if (particle.LifeTime > 0.0)
	{
		// Compute acceleration
		const float groundStiffness = 0.7;
		acceleration.y -= particle.Pos.y <= 0.0 ? particle.Velocity.y / g_timeStep * (groundStiffness + 1.0) : 0.0;
#ifdef GRAVITY
		acceleration.y -= GRAVITY; // Apply gravity
#endif
#ifdef VELOCITY_LOSS
		acceleration -= particle.Velocity * VELOCITY_LOSS;
#endif

		// Integrate and update particle
		particle.Velocity += acceleration * g_timeStep;
		particle.Pos += particle.Velocity * g_timeStep;
		particle.LifeTime -= g_timeStep;
	}
	else particle = Emit(particleId, particle);

	g_rwParticles[particleId] = particle;
}

//--------------------------------------------------------------------------------------
// Particle integration or emission for VS
//--------------------------------------------------------------------------------------
float4 UpdateParticleForVS(uint particleId, inout Particle particle, float3 acceleration)
{
	UpdateParticle(particleId, particle, acceleration);

	const float3 pos = SimulationToWorldSpace(particle.Pos);

	return mul(float4(pos, 1.0), g_viewProj);
}

//--------------------------------------------------------------------------------------
// Vertex shader of particle integration or emission
//--------------------------------------------------------------------------------------
float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_rwParticles[ParticleId];

	return UpdateParticleForVS(ParticleId, particle, 0.0);
}
