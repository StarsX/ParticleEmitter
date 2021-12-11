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
		ComputeUtil();
		ComputeUtil(const Device::sptr& device);
		virtual ~ComputeUtil();

		bool SetPrefixSum(CommandList* pCommandList, bool safeMode,
			const DescriptorTableCache::sptr& descriptorTableCache,
			TypedBuffer* pBuffer, std::vector<Resource::uptr>* pUploaders = nullptr,
			Format format = Format::R32_UINT, uint32_t maxElementCount = 4096);

		void SetDevice(const Device::sptr& device);
		void PrefixSum(CommandList* pCommandList, uint32_t numElements = UINT32_MAX);
		void VerifyPrefixSum(uint32_t numElements = UINT32_MAX);

	protected:
		enum PipelineIndex : uint8_t
		{
			PREFIX_SUM_UINT,
			PREFIX_SUM_SINT,
			PREFIX_SUM_FLOAT,

			PREFIX_SUM_UINT1,
			PREFIX_SUM_UINT2,
			PREFIX_SUM_SINT1,
			PREFIX_SUM_SINT2,
			PREFIX_SUM_FLOAT1,
			PREFIX_SUM_FLOAT2,

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

		Device::sptr m_device;

		Format					m_format;

		XUSG::ShaderPool::uptr				m_shaderPool;
		XUSG::Compute::PipelineCache::uptr	m_computePipelineCache;
		XUSG::PipelineLayoutCache::uptr		m_pipelineLayoutCache;
		XUSG::DescriptorTableCache::sptr	m_descriptorTableCache;

		PipelineLayout			m_pipelineLayouts[NUM_PIPELINE];
		Pipeline				m_pipelines[NUM_PIPELINE];

		TypedBuffer::uptr		m_counter;
		TypedBuffer::uptr		m_testBuffer;
		TypedBuffer::uptr		m_readBack;
		TypedBuffer*			m_pBuffer;

		DescriptorTable			m_uavTables[NUM_UAV_TABLE];

		std::vector<uint8_t>	m_testData;

		bool					m_safeMode;
		uint32_t				m_maxElementCount;
	};
}
