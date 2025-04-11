#pragma once

#include <cuda.h>
#include <d3d11.h>
#include <vector>
#include <memory>
#include <wrl/client.h> // For ComPtr
#include "NvCodec/NvDecoder/NvDecoder.h"
#include "Utils/ColorSpace.h"

#include "MyAssert.h"
#include "Log.h"

class H264Encoder
{
public:
	H264Encoder() = default;
	~H264Encoder() = default;

	bool Init(CUcontext cuContext, int width, int height,
			  Microsoft::WRL::ComPtr<ID3D11Device> pD3D11Device,
			  Microsoft::WRL::ComPtr<ID3D11DeviceContext> pD3D11Context,
			  cudaVideoCodec codecId,
			  unsigned int timescale = 1000)
	{
		if (m_bInitialized)
		{
			throw std::runtime_error("Encoder already initialized");
		}

		m_cuContext = cuContext;
		m_Width = (width + 1) & ~1; // Ensure even width
		m_Height = height;
		m_codecId = codecId;
		m_timescale = timescale;

		m_DxDevice = pD3D11Device;
		m_DxContext = pD3D11Context;

		if (cuMemAlloc(&m_cuFrame, m_Width * m_Height * 4) != CUDA_SUCCESS)
		{
			std::cerr << "Failed to allocate CUDA frame" << std::endl;
			return false;
		}

		m_pEncoder = std::make_unique<NvEncoder>(m_cuContext, true, m_codecId, false, false, NULL, NULL, false, 0, 0, m_timescale);
		m_bInitialized = true;

		D3D11_TEXTURE2D_DESC td;
		// pBackBuffer->GetDesc(&td);
		// D3D11_TEXTURE2D_DESC stagingDesc = {};
		td.Width = width;						// Your texture width
		td.Height = height;						// Your texture height
		td.MipLevels = 1;						// Usually 1 for staging textures
		td.ArraySize = 1;						// Single texture
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 32-bit BGRA format
		td.SampleDesc.Count = 1;				// No multisampling
		td.SampleDesc.Quality = 0;
		td.Usage = D3D11_USAGE_STAGING;				// Critical for CPU access
		td.BindFlags = 0;							// No binding for staging texture
		td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Allow CPU write
		m_DxDevice->CreateTexture2D(&td, NULL, &m_StagingTexture);

		return true;
	}

	bool EncodeFrame(const std::vector<uint8_t> &encodedData,
					 Microsoft::WRL::ComPtr<ID3D11Texture2D> pTexture, int64_t pts = 0)
	{
		MY_ASSERT(m_bInitialized, "Encoder not initialized");
		MY_ASSERT(pTexture, "Texture is not initialized");
		MY_ASSERT(m_pEncoder, "Encoder is not initialized");

		if (encodedData.empty())
		{
			std::cerr << "Encoded data is empty" << std::endl;
			return false;
		}

		int main = 0;
		int64_t timestamp = 0;
		uint8_t *pFrame = nullptr;
		int iMatrix = 0;
		int nFrameReturned = 0;

		nFrameReturned = m_pEncoder->Encode(encodedData.data(), static_cast<int>(encodedData.size()), 0, pts);
		if (!nFrame && nFrameReturned)
		{
			LOG_INFO << m_pEncoder->GetVideoInfo();
		}

		pFrame = m_pEncoder->GetFrame(&timestamp);
		iMatrix = m_pEncoder->GetVideoFormatInfo().video_signal_description.matrix_coefficients;

		if (m_pEncoder->GetBitDepth() == 8)
		{
			if (m_pEncoder->GetOutputFormat() == cudaVideoSurfaceFormat_NV12)
			{
				Nv12ToColor32<BGRA32>(pFrame, m_pEncoder->GetWidth(), (uint8_t *)m_cuFrame, 4 * m_Width, m_pEncoder->GetWidth(), m_pEncoder->GetHeight(), iMatrix);
			}
			else
			{
				LOG_ERROR("Unsupported output format");
				return false;
			}
		}
		else
		{
			LOG_ERROR("Unsupported bit depth");
			return false;
		}

		int pitch = m_Width * 4;

		if (cuCtxPushCurrent(cuContext) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to push CUDA context");
			return false;
		}

		if (cuGraphicsMapResources(1, &cuResource, 0) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to map CUDA resources");
			return false;
		}

		CUarray dstArray;

		if (cuGraphicsSubResourceGetMappedArray(&dstArray, cuResource, 0, 0) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to get mapped array");
			return false;
		}

		CUDA_MEMCPY2D m = {0};
		m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
		m.srcDevice = (CUdeviceptr)m_cuFrame;
		m.srcPitch = pitch;
		m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
		m.dstArray = dstArray;
		m.WidthInBytes = pitch;
		m.Height = m_Height;
		if (cuMemcpy2D(&m) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to copy memory");
			return false;
		}

		if (cuGraphicsUnmapResources(1, &cuResource, 0) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to unmap CUDA resources");
			return false;
		}
		
		if (cuCtxPopCurrent(NULL) != CUDA_SUCCESS)
		{
			LOG_ERROR("Failed to pop CUDA context");
			return false;
		}
		
		return true;
	}

private:
	// CUDA resources
	CUcontext m_cuContext = nullptr;
	CUdeviceptr m_cuFrame = nullptr;

	// DirectX 11 resources
	Microsoft::WRL::ComPtr<ID3D11Device> m_DxDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_DxContext;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_StagingTexture;

	// NvDecoder
	std::unique_ptr<NvEncoder> m_pEncoder;

	// Video properties
	int m_Width;
	int m_Height;
	cudaVideoCodec m_codecId;
	unsigned int m_timescale;
	bool m_bInitialized = false;
};