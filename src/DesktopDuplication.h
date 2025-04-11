#pragma once

#include <d3d11.h>
#include <wrl/client.h> //? for ComPtr
// #include <dxgi.h>
#include <dxgi1_2.h>

#include "Utils.h"

#include "Texture/Texture2D.h"

class DesktopCapture
{
public:
	DesktopCapture(Microsoft::WRL::ComPtr<ID3D11Device> device,
				   Microsoft::WRL::ComPtr<ID3D11DeviceContext> context)
		: m_DxDevice(device), m_DxDeviceContext(context), m_DesktopDuplicationActive(false),
		  m_LastCaptureTime(std::chrono::high_resolution_clock::now())
	{
		m_TargetFrameTime = 1000 / 240; //? use monitor refresh rate in the future
	}

	bool InitDesktopDuplication()
	{
		// Get the DXGI device
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		HRESULT hr = m_DxDevice.As(&dxgiDevice);
		if (FAILED(hr))
		{
			LOG_ERROR("Failed to get DXGI device: {0:x}", hr);
			return false;
		}

		// Get the DXGI adapter
		Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
		hr = dxgiDevice->GetAdapter(&dxgiAdapter);
		if (FAILED(hr))
		{
			LOG_ERROR("Failed to get DXGI adapter: {0:x}", hr);
			return false;
		}

		// Get the primary output (monitor)
		Microsoft::WRL::ComPtr<IDXGIOutput> dxgiOutput;
		hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
		if (FAILED(hr))
		{
			LOG_ERROR("Failed to get DXGI output: {0:x}", hr);
			return false;
		}

		// Query for Output1 interface
		Microsoft::WRL::ComPtr<IDXGIOutput1> dxgiOutput1;
		hr = dxgiOutput.As(&dxgiOutput1);
		if (FAILED(hr))
		{
			LOG_ERROR("Failed to get IDXGIOutput1: {0:x}", hr);
			return false;
		}

		// Create the desktop duplication interface
		hr = dxgiOutput1->DuplicateOutput(m_DxDevice.Get(), &m_DxgiDesktopDuplication);
		if (FAILED(hr))
		{
			if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
			{
				LOG_ERROR("Desktop duplication is not currently available (limit of 1 duplication per output)");
			}
			else
			{
				LOG_ERROR("Failed to create desktop duplication: {0:x}", hr);
			}
			return false;
		}

		// Get the output description
		m_DxgiDesktopDuplication->GetDesc(&m_DxgiOutputDesc);
		// LOG_INFO("Desktop duplication initialized: {0}x{1} format {2}",
		// 		 m_DxgiOutputDesc.ModeDesc.Width,
		// 		 m_DxgiOutputDesc.ModeDesc.Height,
		// 		 m_dxgiOutputDesc.ModeDesc.Format);

		m_OutputDesktopTexture.CreateWithCustomFlags(
			m_DxDevice.Get(),
			m_DxgiOutputDesc.ModeDesc.Width,
			m_DxgiOutputDesc.ModeDesc.Height,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
			Texture2D::TextureFormat::BGRA8_UNORM);

		m_DesktopDuplicationActive = true;
		return true;
	}

	void CaptureFrame()
	{
		MY_ASSERT(m_DxDeviceContext != nullptr, "Device context is null");
		// MY_ASSERT(m_OutputDesktopTexture.IsValid(), "Desktop texture is not valid");

		auto currentTime = std::chrono::high_resolution_clock::now();
		auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
							 currentTime - m_LastCaptureTime)
							 .count();

		if (elapsedMs < m_TargetFrameTime)
		{
			return; // Skip this frame if the target frame time has not passed
		}
		m_LastCaptureTime = currentTime;
		// Try to get the next frame
		{
			// ScopedTimer timer("        Acquire Next Frame");
			DXGI_OUTDUPL_FRAME_INFO frameInfo;
			Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;
			HRESULT hr = m_DxgiDesktopDuplication->AcquireNextFrame(0, &frameInfo, &desktopResource);

			if (hr == DXGI_ERROR_WAIT_TIMEOUT)
			{
				// No new frame available, just return
				return;
			}
			else if (FAILED(hr))
			{
				LOG_ERROR("Failed to acquire next frame: 0x{0:08x}", hr);

				// Consider reinitializing the duplication if a critical error occurs
				if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL)
				{
					LOG_WARN("Desktop duplication needs to be reinitialized");
					// You might want to set a flag to reinitialize the duplication
				}
				return;
			}

			// Get the desktop texture
			{
				// ScopedTimer timer("        Query Interface");
				Microsoft::WRL::ComPtr<ID3D11Texture2D> acquiredDesktopTexture;
				hr = desktopResource.As(&acquiredDesktopTexture);
				if (FAILED(hr))
				{
					LOG_ERROR("Failed to QI for ID3D11Texture2D: 0x{0:08x}", hr);
					m_DxgiDesktopDuplication->ReleaseFrame();
					return;
				}

				// Copy the desktop texture to our texture
				{
					// ScopedTimer timer("        Copy Resource");
					m_DxDeviceContext->CopyResource(m_OutputDesktopTexture.GetTexture(), acquiredDesktopTexture.Get());
				}
			}
			m_DxgiDesktopDuplication->ReleaseFrame();
		}
	}

	const Texture2D GetTexture() const
	{
		return m_OutputDesktopTexture;
	}

	uint32_t GetWidth() const
	{
		return m_OutputDesktopTexture.GetWidth();
	}

	uint32_t GetHeight() const
	{
		return m_OutputDesktopTexture.GetHeight();
	}

private:
	Microsoft::WRL::ComPtr<ID3D11Device> m_DxDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_DxDeviceContext;

	Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_DxgiDesktopDuplication;
	DXGI_OUTDUPL_DESC m_DxgiOutputDesc;

	Texture2D m_OutputDesktopTexture;

	bool m_DesktopDuplicationActive = false;

	std::chrono::high_resolution_clock::time_point m_LastCaptureTime;
	double m_TargetFrameTime = 0.0;
};