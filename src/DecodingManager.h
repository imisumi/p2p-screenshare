#pragma once

#include <unordered_map>

#include <cuda.h>

#include "Utils.h"

#include <d3d11.h>

#include <wrl/client.h>

#include "Texture/Texture2D.h"

#include "Codec/Decoder.h"

struct DecoderData
{
	std::unique_ptr<Texture2D> DecodedTexture;
	// NvDecoderDX11 NvDecoderDX11;
	std::unique_ptr<NvDecoderDX11> NvDecoderDX11;
};

class DecodingManager
{
public:
	DecodingManager()
	{
		if (cuInit(0) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to initialize CUDA");
			throw std::runtime_error("Failed to initialize CUDA");
			return;
		}
		cuDeviceGet(&m_CuDevice, 0);
		if (cuCtxCreate(&m_CuContext, CU_CTX_SCHED_BLOCKING_SYNC, m_CuDevice) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to create CUDA context");
			throw std::runtime_error("Failed to create CUDA context");
			return;
		}
	}

	~DecodingManager()
	{
		if (m_CuContext)
		{
			cuCtxDestroy(m_CuContext);
			m_CuContext = nullptr;
		}
		for (auto &decoder : m_DecoderDataMap)
		{
		}
		LOG_INFO("DecodingManager destructor completed successfully");
	}

	void SetDxDevice(Microsoft::WRL::ComPtr<ID3D11Device> dxDevice,
					 Microsoft::WRL::ComPtr<ID3D11DeviceContext> dxContext)
	{
		m_DxDevice = dxDevice;
		m_DxContext = dxContext;
	}

	bool AddStream(const std::string &streamId, uint32_t width, uint32_t height)
	{
		MY_ASSERT(m_CuContext != nullptr, "CUDA context is not initialized");
		MY_ASSERT(m_DxDevice != nullptr, "DX11 device is not initialized");
		MY_ASSERT(m_DxContext != nullptr, "DX11 context is not initialized");
		DecoderData decoderData;
		decoderData.DecodedTexture = std::make_unique<Texture2D>();
		decoderData.NvDecoderDX11 = std::make_unique<NvDecoderDX11>();
		if (!decoderData.DecodedTexture->CreateWithCustomFlags(
				m_DxDevice.Get(),
				width,
				height,
				D3D11_BIND_SHADER_RESOURCE,
				D3D11_RESOURCE_MISC_SHARED,
				Texture2D::TextureFormat::BGR8))
		{
			LOG_ERROR("Failed to create decoded texture for stream: {0}", streamId);
			return false;
		}
		if (!decoderData.NvDecoderDX11->Initialize(m_CuContext, m_DxDevice, m_DxContext,
												   cudaVideoCodec_H264, // Example codec, adjust as needed
												   width, height))
		{
			LOG_ERROR("Failed to initialize decoder for stream: {0}", streamId);
			return false;
		}

		m_DecoderDataMap[streamId] = std::move(decoderData);

		return true;
	}

	bool DecodeStream(const std::string &streamId, const std::vector<uint8_t> &encodedData)
	{
		MY_ASSERT(m_CuContext != nullptr, "CUDA context is not initialized");
		MY_ASSERT(m_DxDevice != nullptr, "DX11 device is not initialized");
		MY_ASSERT(m_DxContext != nullptr, "DX11 context is not initialized");
		if (!m_DecoderDataMap.contains(streamId))
		{
			LOG_ERROR("Stream ID not found: {0}", streamId);
			return false;
		}
		auto &decoderData = m_DecoderDataMap[streamId];
		if (!decoderData.NvDecoderDX11->DecodeFrame(encodedData, decoderData.DecodedTexture->GetTexture()))
		{
			LOG_ERROR("Failed to decode stream: {0}", streamId);
			return false;
		}
		return true;
	}

	ID3D11ShaderResourceView *GetDecodedTextureSRV(const std::string &streamId)
	{
		if (!m_DecoderDataMap.contains(streamId))
		{
			LOG_ERROR("Stream ID not found: {0}", streamId);
			return nullptr;
		}
		return m_DecoderDataMap[streamId].DecodedTexture->GetShaderResourceView();
	}

private:
	CUdevice m_CuDevice = 0;
	CUcontext m_CuContext = nullptr;
	std::unordered_map<std::string, DecoderData> m_DecoderDataMap;

	Microsoft::WRL::ComPtr<ID3D11Device> m_DxDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_DxContext;
};