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

static float4 g_boundary = { 0.0.xxx, 16.0 };

//--------------------------------------------------------------------------------------
// Transform to grid space
//--------------------------------------------------------------------------------------
int3 ToGridSpace(float3 pos)
{
	const float halfGridSize = GRID_SIZE * 0.5;
	pos = (pos - g_boundary.xyz) / g_boundary.w; // [-1, 1]

	return pos * halfGridSize + halfGridSize;
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
	return GridGetCellIndex(ToGridSpace(pos));
}
