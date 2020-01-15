#include "XUSGComputeUtil.h"

using namespace std;
//using namespace DirectX;
using namespace XUSG;

ComputeUtil::ComputeUtil(const Device& device) :
	m_device(device)
{
	m_computePipelineCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
}

ComputeUtil::~ComputeUtil()
{
}

bool ComputeUtil::SetPrefixSum(const CommandList& commandList,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	const Descriptor* pBufferView, vector<Resource>* pUploaders,
	uint32_t maxElementCount)
{
	if (maxElementCount > 1024 * 1024)
		assert(!"Error: maxElementCount should be no more than 1048576!");
	m_descriptorTableCache = descriptorTableCache;

	// Create resources
	N_RETURN(m_counter.Create(m_device, 1, sizeof(uint32_t), Format::R32_FLOAT,
		ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::DENY_SHADER_RESOURCE,
		MemoryType::DEFAULT, 0, nullptr, 1, nullptr, L"GlobalBarrierCounter"), false);

	if (pBufferView)
	{
		// Create a UAV table
		{
			Util::DescriptorTable uavTable;
			uavTable.SetDescriptors(0, 1, pBufferView);
			X_RETURN(m_uavTables[UAV_TABLE_DATA], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
		}

		// Append a counter UAV table
		{
			Util::DescriptorTable uavTable;
			uavTable.SetDescriptors(0, 1, &m_counter.GetUAV());
			X_RETURN(m_uavTables[UAV_TABLE_COUNTER], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
		}
	}
	else
	{
		// Create test buffers
		m_testBuffer = make_unique<StructuredBuffer>();
		N_RETURN(m_testBuffer->Create(m_device, maxElementCount, sizeof(uint32_t),
			ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::DENY_SHADER_RESOURCE,
			MemoryType::DEFAULT, 0, nullptr, 1, nullptr, L"PrefixSumTestBuffer"), false);

		m_readBack = make_unique<StructuredBuffer>();
		N_RETURN(m_readBack->Create(m_device, maxElementCount, sizeof(uint32_t),
			ResourceFlag::DENY_SHADER_RESOURCE, MemoryType::READBACK, 0,
			nullptr, 0, nullptr, L"ReadBackBuffer"), false);

		// Create a UAV table
		{
			Util::DescriptorTable uavTable;
			uavTable.SetDescriptors(0, 1, &m_testBuffer->GetUAV(), TEMPORARY_POOL);
			X_RETURN(m_uavTables[UAV_TABLE_DATA], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
		}

		// Append a counter UAV table
		{
			Util::DescriptorTable uavTable;
			uavTable.SetDescriptors(0, 1, &m_counter.GetUAV(), TEMPORARY_POOL);
			X_RETURN(m_uavTables[UAV_TABLE_COUNTER], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
		}

		// Upload test data
		m_testData.resize(maxElementCount);
		for (auto& data : m_testData) data = rand();

		if (!pUploaders) assert(!"Error: if pBufferView is nullptr, pUploaders must not be nullptr!");
		pUploaders->emplace_back();
		m_testBuffer->Upload(commandList, pUploaders->back(), m_testData.data(),
			sizeof(uint32_t) * maxElementCount, 0, ResourceState::UNORDERED_ACCESS);
	}

	// Create pipeline layout
	Util::PipelineLayout pipelineLayout;
	pipelineLayout.SetConstants(0, SizeOfInUint32(uint32_t), 0);
	pipelineLayout.SetRange(1, DescriptorType::UAV, 2, 0);
	X_RETURN(m_pipelineLayouts[PREFIX_SUM], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
		PipelineLayoutFlag::NONE, L"PrefixSumLayout"), false);

	// Create pipeline
	N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, PREFIX_SUM, L"CSPrefixSum.cso"), false);

	Compute::State state;
	state.SetPipelineLayout(m_pipelineLayouts[PREFIX_SUM]);
	state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, PREFIX_SUM));
	X_RETURN(m_pipelines[PREFIX_SUM], state.GetPipeline(m_computePipelineCache, L"PrefixSum"), false);

	return false;
}

void ComputeUtil::PrefixSum(const CommandList& commandList, uint32_t numElements)
{
	if (!m_testData.empty() && numElements > m_testData.size())
		assert(!"Error: numElements is greater than maxElementCount!");

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL,
			m_testBuffer ? TEMPORARY_POOL : PERMANENT_POOL),
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Clear counter
	uint32_t clear[4] = {};
	commandList.ClearUnorderedAccessViewUint(m_uavTables[UAV_TABLE_COUNTER],
		m_counter.GetUAV(), m_counter.GetResource(), clear);

	// Set pipeline state
	commandList.SetComputePipelineLayout(m_pipelineLayouts[PREFIX_SUM]);
	commandList.SetPipelineState(m_pipelines[PREFIX_SUM]);

	// Set descriptor tables
	const auto numGroups = DIV_UP(numElements, 1024);
	commandList.SetCompute32BitConstants(0, SizeOfInUint32(numGroups), &numGroups);
	commandList.SetComputeDescriptorTable(1, m_uavTables[UAV_TABLE_DATA]);

	commandList.Dispatch(numGroups, 1, 1);

	if (m_readBack)
	{
		assert(m_testBuffer);

		// Set barrier
		ResourceBarrier barrier;
		m_testBuffer->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS); // Promotion
		const auto numBarriers = m_testBuffer->SetBarrier(&barrier, ResourceState::COPY_SOURCE);
		commandList.Barrier(numBarriers, &barrier);

		// Copy the counter for readback
		commandList.CopyResource(m_readBack->GetResource(), m_testBuffer->GetResource());
	}
}

void ComputeUtil::VerifyPrefixSum()
{
	vector<uint32_t> groundTruths(m_testData.size());

	// Generate ground truth
	for (size_t i = 1; i < m_testData.size(); ++i)
		groundTruths[i] = groundTruths[i - 1] + m_testData[i - 1];

	const auto testResults = reinterpret_cast<const uint32_t*>(m_readBack->Map(nullptr));
	for (size_t i = 0; i < m_testData.size(); ++i)
	{
		if (testResults[i] != groundTruths[i])
			cout << "Wrong " << i << ": input ( " << m_testData[i] <<
			"), result (" << testResults[i] << "), ground truth (" <<
			groundTruths[i] << ")" << endl;
		else
			cout << "Correct " << i << ": input ( " << m_testData[i] <<
			"), result (" << testResults[i] << "), ground truth (" <<
			groundTruths[i] << ")" << endl;
		if (i % 1024 == 1023) system("pause");
	}
	m_readBack->Unmap();
}
