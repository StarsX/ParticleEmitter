//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define GRID_SIZE 32

static float4 g_boundary = { 0.0.xxx, 16.0 };

int3 ToGridSpace(float3 pos)
{
	const float halfGridSize = GRID_SIZE * 0.5;
	pos = (pos - g_boundary.xyz) / g_boundary.w; // [-1, 1]

	return pos * halfGridSize + halfGridSize;
}

uint GetGridBinIndex(int3 pos)
{
	return pos.z * GRID_SIZE * GRID_SIZE +
		pos.y * GRID_SIZE + pos.x;
}

bool IsOutOfGrid(int3 pos)
{
	return any(pos < 0) || any(pos >= GRID_SIZE);
}
