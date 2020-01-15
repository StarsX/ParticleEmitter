//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define GROUP_SIZE 1024

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------
const uint g_numGroups;

//--------------------------------------------------------------------------------------
// Buffer
//--------------------------------------------------------------------------------------
RWStructuredBuffer<uint> g_rwData;
RWStructuredBuffer<uint> g_rwCounter;

groupshared uint g_waveSums[64];
groupshared uint g_counter;

//--------------------------------------------------------------------------------------
// Per-group prefix sum
//--------------------------------------------------------------------------------------
uint GroupPrefixSum(uint value, uint GIdx : SV_GroupIndex)
{
	// Supposing we have 3 0 5 7, 2 9 0 10, 0 4 1 8 in a group,
	// and wave size = 4
	// Per-wave prefix sum
	// => 0 3 3 8, 0 2 11 11, 0 0 4 5
	const uint sum = WavePrefixSum(value);

	// Get wave size and wave index
	const uint waveSize = WaveGetLaneCount();
	const uint waveIdx = GIdx / waveSize;
	// assert(waveIdx < waveSize);

	// Calculate for the total sum of the wave
	if (WaveGetLaneIndex() == waveSize - 1) // Is the last lane
		// Write the wave total sum to the group shared memory
		// 15 = sum(8) + value(7)
		// 21 = sum(11) + value(10)
		// 13 = sum(5) + value(8)
		g_waveSums[waveIdx] = sum + value;
	GroupMemoryBarrierWithGroupSync();

	const uint numWaves = GROUP_SIZE / waveSize;
	// assert(numWaves <= waveSize);
	if (GIdx < numWaves)
	{
		// Move the previous-round wave total sums into one wave,
		// and then do per-wave prefix sum
		// 15 21 13 => 0 15 36
		const uint value = g_waveSums[GIdx];
		const uint sum = WavePrefixSum(value);

		// Write the prefix-summed total sum of the previous-round wave to the group shared memory
		g_waveSums[GIdx] = sum;
	}
	GroupMemoryBarrierWithGroupSync();

	// 0 3 3 8, 0 2 11 11, 0 0 4 5
	// => (0 + 0) (3 + 0) (3 + 0) (8 + 0), (0 + 15) (2 + 15) (11 + 15) (11 + 15), (0 + 36) (0 + 36) (4 + 36) (5 + 36)
	// => 0 3 3 8, 15 17 26 26, 36 36 40 41
	return sum + g_waveSums[waveIdx];
}

//--------------------------------------------------------------------------------------
// If the current group is the slowest
//--------------------------------------------------------------------------------------
bool IsSlowestGroup(uint GIdx : SV_GroupIndex)
{
	if (GIdx == 0) InterlockedAdd(g_rwCounter[0], 1, g_counter);
	AllMemoryBarrierWithGroupSync();

	return g_counter + 1 == g_numGroups;
}

//--------------------------------------------------------------------------------------
// The slowest group finished
//--------------------------------------------------------------------------------------
void SlowestGroupFinished(bool isSlowestGroup, uint GIdx : SV_GroupIndex)
{
	if (isSlowestGroup && GIdx == 0) g_rwCounter[0] = g_numGroups + 1;
}

void WaitForSlowestGroup()
{
	[allow_uav_condition]
	while (DeviceMemoryBarrier(), g_rwCounter[0] <= g_numGroups);
}

//--------------------------------------------------------------------------------------
// 1-pass global prefix-sum for at most 1024 * 1024 uints
//-------------------------------------------------------------------------------------
[numthreads(GROUP_SIZE, 1, 1)]
void main(uint DTid : SV_DispatchThreadID, uint GIdx : SV_GroupIndex, uint Gid : SV_GroupID)
{
	// Per-group prefix sum
	const uint value = g_rwData[DTid];
	const uint sum = GroupPrefixSum(value, GIdx);
	g_rwData[DTid] = sum;

	// Leave the slowest group
	const bool isSlowestGroup = IsSlowestGroup(GIdx);
	if (isSlowestGroup)
	{
		// Load the last value of the group in the previous round
		const uint value = g_rwData[GROUP_SIZE * (GIdx + 1) - 1];
		const uint sum = GroupPrefixSum(value, GIdx);

		// The first element of each group is always 0.
		g_rwData[GROUP_SIZE * GIdx] = sum;
	}
	DeviceMemoryBarrierWithGroupSync();
	SlowestGroupFinished(isSlowestGroup, GIdx);
	
	// Wait for the slowest group
	WaitForSlowestGroup();

	if (GIdx > 0) g_rwData[DTid] += g_rwData[GROUP_SIZE * Gid];
}
