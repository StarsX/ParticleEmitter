//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"

namespace XUSG
{
	class ComputeUtil
	{
	public:
		ComputeUtil(const Device& device);
		virtual ~ComputeUtil();

		bool SetPrefixSum(const CommandList& commandList,
			std::shared_ptr<DescriptorTableCache> descriptorTableCache,
			const Descriptor* pBufferView, std::vector<Resource>* pUploaders = nullptr,
			uint32_t maxElementCount = 4096);

		void PrefixSum(const CommandList& commandList, uint32_t numElements);
		void VerifyPrefixSum();

	protected:
		enum PipelineIndex : uint8_t
		{
			PREFIX_SUM,

			NUM_PIPELINE
		};

		enum DescriptorPoolIndex : uint8_t
		{
			PERMANENT_POOL,
			TEMPORARY_POOL
		};

		enum UAVTable : uint8_t
		{
			UAV_TABLE_DATA,
			UAV_TABLE_COUNTER,

			NUM_UAV_TABLE
		};

		Device m_device;

		ShaderPool				m_shaderPool;
		Compute::PipelineCache	m_computePipelineCache;
		PipelineLayoutCache		m_pipelineLayoutCache;
		std::shared_ptr<DescriptorTableCache> m_descriptorTableCache;

		PipelineLayout			m_pipelineLayouts[NUM_PIPELINE];
		Pipeline				m_pipelines[NUM_PIPELINE];

		TypedBuffer				m_counter;
		std::unique_ptr<StructuredBuffer> m_testBuffer;
		std::unique_ptr<StructuredBuffer> m_readBack;

		DescriptorTable			m_uavTables[NUM_UAV_TABLE];

		std::vector<uint32_t>	m_testData;
	};
}
