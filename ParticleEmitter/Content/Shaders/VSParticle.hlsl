//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#ifndef main
#define main mainCS
#include "CSEmit.hlsl"
#undef main
#endif

#define VELOCITY_LOSS 1.0

float4 Update(uint particleId, inout Particle particle)
{
	if (particle.LifeTime > 0.0)
	{
		// Integrate and update particle
		particle.Velocity.y -= 9.8 * g_timeStep;
		particle.Pos += particle.Velocity * g_timeStep;
		particle.Velocity *= max(1.0 - VELOCITY_LOSS * g_timeStep, 0.0);
		particle.LifeTime -= g_timeStep;
	}
	else particle = Emit(particleId, particle);

	g_rwParticles[particleId] = particle;

	return mul(float4(particle.Pos, 1.0), g_viewProj);
}

float4 main(uint ParticleId : SV_VERTEXID) : SV_POSITION
{
	// Load particle
	Particle particle = g_rwParticles[ParticleId];

	return Update(ParticleId, particle);
}
