//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define GRID_SIZE 32

#define GET_CELL_INDEX(i, p, r) \
	GridGetCellIndexWithPosition(p); \
	if (i == 0xffffffff) return r

//--------------------------------------------------------------------------------------
// Struct
//--------------------------------------------------------------------------------------
struct Particle
{
	float3 Pos;
	float3 Velocity;
	float LifeTime;
};

static float4 g_boundary = { 0.0.xxx, 3.2 };

//--------------------------------------------------------------------------------------
// Transform from simulation space to grid space
//--------------------------------------------------------------------------------------
int3 SimulationToGridSpace(float3 v)
{
	const float halfGridSize = GRID_SIZE * 0.5;
	v = (v - g_boundary.xyz) / g_boundary.w; // [-1, 1]

	return v * halfGridSize + halfGridSize;
}

//--------------------------------------------------------------------------------------
// Transform from world space to simulation space
//--------------------------------------------------------------------------------------
float3 WorldToSimulationSpace(float3 v)
{
	return v * 0.1;
}

//--------------------------------------------------------------------------------------
// Transform from simulation space to world space
//--------------------------------------------------------------------------------------
float3 SimulationToWorldSpace(float3 v)
{
	return v / 0.1;
}


//--------------------------------------------------------------------------------------
// Out of grid boundary
//--------------------------------------------------------------------------------------
bool IsOutOfGrid(int3 pos)
{
	return any(pos < 0) || any(pos >= GRID_SIZE);
}

//--------------------------------------------------------------------------------------
// Hash to linear index
//--------------------------------------------------------------------------------------
uint GridGetCellIndex(int3 pos)
{
	const int3 strides = int3(1, GRID_SIZE, GRID_SIZE * GRID_SIZE);

	return IsOutOfGrid(pos) ? 0xffffffff : dot(pos, strides);
}

//--------------------------------------------------------------------------------------
// Hash to linear index
//--------------------------------------------------------------------------------------
uint GridGetCellIndexWithPosition(float3 pos)
{
	return GridGetCellIndex(SimulationToGridSpace(pos));
}
