#pragma once

#include <cuda.h>
#include <d3d11.h>
#include <vector>
#include <memory>
#include <wrl/client.h> // For ComPtr
#include "NvCodec/NvDecoder/NvDecoder.h"
#include "Utils/ColorSpace.h"
#include <cuda_d3d11_interop.h>
#include <cudaD3D11.h>

/**
 * @class NvDecoderDX11
 * @brief Class for decoding encoded video data and converting to DX11 textures
 *
 * This class handles initialization of NVIDIA decoder and conversion of
 * decoded frames to DX11 textures using CUDA-DX11 interop.
 */
class NvDecoderDX11
{
public:
	/**
	 * @brief Constructor
	 */
	NvDecoderDX11() : m_cuContext(nullptr),
					  m_pDecoder(nullptr),
					  m_dpFrame(0),
					  m_bInitialized(false),
					  m_nRGBWidth(0),
					  m_nHeight(0),
					  m_codecId(cudaVideoCodec_H264) {}

	/**
	 * @brief Destructor
	 */
	~NvDecoderDX11()
	{
		Cleanup();
	}

	/**
	 * @brief Initialize the decoder
	 *
	 * @param cuContext CUDA context
	 * @param pD3D11Device ComPtr to D3D11 device
	 * @param pD3D11Context ComPtr to D3D11 device context
	 * @param codecId Codec ID for the encoded data
	 * @param width Width of the video
	 * @param height Height of the video
	 * @param timescale Timescale for timestamps
	 * @return true if initialization successful, false otherwise
	 */
	bool Initialize(CUcontext cuContext,
					Microsoft::WRL::ComPtr<ID3D11Device> pD3D11Device,
					Microsoft::WRL::ComPtr<ID3D11DeviceContext> pD3D11Context,
					cudaVideoCodec codecId, int width, int height,
					unsigned int timescale = 1000)
	{
		if (m_bInitialized)
		{
			Cleanup();
		}

		try
		{
			m_cuContext = cuContext;
			m_codecId = codecId;
			m_nRGBWidth = (width + 1) & ~1; // Ensure even width
			m_nHeight = height;
			m_timescale = timescale;

			// Create the decoder with explicit codec enum cast
			m_pDecoder = new NvDecoder(m_cuContext, true, static_cast<cudaVideoCodec>(m_codecId), false, false);

			// Allocate device memory for the RGB frame
			if (m_dpFrame == 0)
			{
				ck(cuMemAlloc(&m_dpFrame, m_nRGBWidth * m_nHeight * 4)); // 4 bytes per pixel (BGRA)
			}

			m_pD3D11Device = pD3D11Device;
			m_pD3D11Context = pD3D11Context;

			m_bInitialized = true;
			return true;
		}
		catch (const std::exception &ex)
		{
			std::cerr << "Initialization failed: " << ex.what() << std::endl;
			Cleanup();
			return false;
		}
	}

	/**
	 * @brief Decode a frame and convert to a DX11 texture
	 *
	 * @param encodedData Vector of encoded video data
	 * @param pTexture ComPtr to the texture to update
	 * @param pts Presentation timestamp
	 * @return true if decoding and texture update successful, false otherwise
	 */
	bool DecodeFrame(const std::vector<uint8_t> &encodedData,
					 ID3D11Texture2D *pTexture, int64_t pts = 0)
	{
		// if (!m_bInitialized || !m_pDecoder || !pTexture)
		// {
		// 	std::cerr << "Decoder not initialized or invalid parameters" << std::endl;
		// 	return false;
		// }
		if (!m_bInitialized)
		{
			std::cerr << "Decoder not initialized" << std::endl;
			return false;
		}
		if (!m_pDecoder)
		{
			std::cerr << "Decoder is null" << std::endl;
			return false;
		}
		if (!pTexture)
		{
			std::cerr << "Texture is null" << std::endl;
			return false;
		}

		if (encodedData.empty())
		{
			std::cerr << "Encoded data is empty" << std::endl;
			return false;
		}

		try
		{
			int nFrameReturned = 0;
			int64_t timestamp = 0;
			uint8_t *pFrame = nullptr;
			int iMatrix = 0;

			// Decode the frame
			nFrameReturned = m_pDecoder->Decode(encodedData.data(), static_cast<int>(encodedData.size()), 0, pts);

			if (nFrameReturned <= 0)
			{
				// No frames returned, this is not necessarily an error (could be B-frame)
				std::cerr << "No frames returned" << std::endl;
				return false;
			}

			// After successful decoding, get the frame
			pFrame = m_pDecoder->GetFrame(&timestamp);
			if (!pFrame)
			{
				std::cerr << "GetFrame returned null pointer despite successful Decode call" << std::endl;
				return false;
			}

			// Get the color matrix coefficient for proper conversion
			iMatrix = m_pDecoder->GetVideoFormatInfo().video_signal_description.matrix_coefficients;
			if (iMatrix == 0)
			{
				// Default to BT.709 if not specified
				iMatrix = ColorSpaceStandard_BT709;
			}

			// Make sure the CUDA context is current
			CUcontext current;
			ck(cuCtxPushCurrent(m_cuContext));

			// Convert to RGB format appropriate for texture
			if (m_pDecoder->GetOutputFormat() == cudaVideoSurfaceFormat_NV12)
			{
				// std::cout << "Converting NV12 to BGRA..." << std::endl;
				// Convert NV12 to BGRA using NVIDIA's converter
				// Nv12ToColor32<RGBA32>(
				Nv12ToColor32<BGRA32>(
					pFrame,
					m_pDecoder->GetDeviceFramePitch(),
					(uint8_t *)m_dpFrame,
					4 * m_nRGBWidth,
					m_pDecoder->GetWidth(),
					m_pDecoder->GetHeight(),
					iMatrix,
					false // Limited range
				);
			}
			else
			{
				// Pop context before throwing
				ck(cuCtxPopCurrent(&current));
				throw std::runtime_error("Unsupported output format");
			}

			// Ensure conversion is complete
			ck(cuCtxSynchronize());

			// Pop the context
			ck(cuCtxPopCurrent(&current));

			// Copy the frame to the DX11 texture
			return UpdateTexture(pTexture);
		}
		catch (const std::exception &ex)
		{
			std::cerr << "Decoding failed: " << ex.what() << std::endl;
			return false;
		}
	}


	/**
	 * @brief Cleanup resources
	 */
	void Cleanup()
	{
		if (m_dpFrame)
		{
			cuMemFree(m_dpFrame);
			m_dpFrame = 0;
		}

		if (m_pDecoder)
		{
			delete m_pDecoder;
			m_pDecoder = nullptr;
		}

		m_pD3D11Device = nullptr;
		m_pD3D11Context = nullptr;
		m_bInitialized = false;
	}

	/**
	 * @brief Get information about the video
	 *
	 * @return String containing video information or empty string if not initialized
	 */
	std::string GetVideoInfo() const
	{
		if (m_pDecoder)
		{
			return m_pDecoder->GetVideoInfo();
		}
		return "";
	}

private:
	/**
	 * @brief Update DX11 texture with decoded frame data
	 *
	 * @param pTexture Pointer to the texture to update
	 * @return true if update successful, false otherwise
	 */
	bool UpdateTexture(ID3D11Texture2D *pTexture)
	{
		if (!pTexture || !m_pD3D11Context)
		{
			std::cerr << "Texture or context is not initialized" << std::endl;
			return false;
		}

		// Get texture description
		D3D11_TEXTURE2D_DESC desc;
		pTexture->GetDesc(&desc);

		// Verify dimensions match
		if (desc.Width != static_cast<UINT>(m_nRGBWidth) || desc.Height != static_cast<UINT>(m_nHeight))
		{
			std::cerr << "Texture dimensions do not match decoder dimensions" << std::endl;
			return false;
		}

		// Register the texture with CUDA if not already registered
		static CUgraphicsResource cuResource = NULL;
		static ID3D11Texture2D *registeredTexture = nullptr;

		if (registeredTexture != pTexture)
		{
			// Unregister previous texture if any
			if (cuResource)
			{
				cuGraphicsUnregisterResource(cuResource);
				cuResource = NULL;
			}

			// Register the texture with CUDA
			// HRESULT hr = pTexture->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_MAXIMUM);
			// if (FAILED(hr)) {
			// 	std::cerr << "Failed to set texture priority" << std::endl;
			// }
			pTexture->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_MAXIMUM);

			// CUresult result = cuGraphicsD3D11RegisterResource(&cuResource, pTexture,
			// 												CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
			CUresult result = cuGraphicsD3D11RegisterResource(&cuResource, pTexture,
															  CU_GRAPHICS_REGISTER_FLAGS_NONE);
			if (result != CUDA_SUCCESS)
			{
				std::cerr << "Failed to register D3D11 texture with CUDA: " << result << std::endl;
				return false;
			}

			// Set flags for how the resource will be used by CUDA
			result = cuGraphicsResourceSetMapFlags(cuResource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
			if (result != CUDA_SUCCESS)
			{
				std::cerr << "Failed to set resource mapping flags: " << result << std::endl;
				cuGraphicsUnregisterResource(cuResource);
				return false;
			}

			registeredTexture = pTexture;
		}

		// Map the resource to get CUDA access
		CUresult result = cuGraphicsMapResources(1, &cuResource, 0);
		if (result != CUDA_SUCCESS)
		{
			std::cerr << "Failed to map CUDA resource: " << result << std::endl;
			return false;
		}

		// Get a CUDA array from the resource
		CUarray cuArray = NULL;
		result = cuGraphicsSubResourceGetMappedArray(&cuArray, cuResource, 0, 0);
		if (result != CUDA_SUCCESS)
		{
			std::cerr << "Failed to get mapped array: " << result << std::endl;
			cuGraphicsUnmapResources(1, &cuResource, 0);
			return false;
		}

		// Define the copy parameters
		CUDA_MEMCPY2D copyParams = {0};
		copyParams.srcMemoryType = CU_MEMORYTYPE_DEVICE;
		copyParams.srcDevice = m_dpFrame;
		copyParams.srcPitch = m_nRGBWidth * 4; // 4 bytes per pixel (BGRA/BGRX)

		copyParams.dstMemoryType = CU_MEMORYTYPE_ARRAY;
		copyParams.dstArray = cuArray;

		copyParams.WidthInBytes = m_nRGBWidth * 4;
		copyParams.Height = m_nHeight;

		// Copy the data
		result = cuMemcpy2D(&copyParams);
		if (result != CUDA_SUCCESS)
		{
			std::cerr << "Failed to copy data to texture: " << result << std::endl;
			cuGraphicsUnmapResources(1, &cuResource, 0);
			return false;
		}

		// Unmap the resource
		result = cuGraphicsUnmapResources(1, &cuResource, 0);
		if (result != CUDA_SUCCESS)
		{
			std::cerr << "Failed to unmap CUDA resource: " << result << std::endl;
			return false;
		}

		return true;
	}

private:
	// CUDA resources
	CUcontext m_cuContext;
	CUdeviceptr m_dpFrame;

	// DirectX 11 resources
	Microsoft::WRL::ComPtr<ID3D11Device> m_pD3D11Device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pD3D11Context;

	// NvDecoder
	NvDecoder *m_pDecoder;

	// State tracking
	bool m_bInitialized;

	// Video properties
	int m_nRGBWidth;
	int m_nHeight;
	cudaVideoCodec m_codecId;
	unsigned int m_timescale;
};