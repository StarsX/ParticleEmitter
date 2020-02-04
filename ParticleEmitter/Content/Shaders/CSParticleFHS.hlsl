//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define FOR_CS 1
#include "VSParticleFHF.hlsl"

//--------------------------------------------------------------------------------------
// Compute shader of particle integration or emission for fast hybrid fluid
//--------------------------------------------------------------------------------------
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	UpdateParticleFHF(DTid.x);
}
