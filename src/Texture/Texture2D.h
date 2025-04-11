#pragma once

#include <d3d11.h>
#include <vector>
#include <memory>
#include <string>
#include <wrl/client.h>
#include <span>

class Texture2D
{
public:
	enum class TextureType
	{
		DEFAULT,   // Standard GPU-only texture
		DYNAMIC,   // GPU texture optimized for frequent CPU updates
		STAGING,   // CPU-readable for transferring data
		CPU_READ,  // CPU can read but not write
		CPU_WRITE, // CPU can write but not read
		CPU_RW	   // CPU can both read and write
	};

	enum class TextureFormat
	{
		RGBA8_UNORM, // DXGI_FORMAT_R8G8B8A8_UNORM
		BGRA8_UNORM, // DXGI_FORMAT_B8G8R8A8_UNORM
		// DXGI_FORMAT_B8G8R8X8_UNORM,
		BGR8
	};

	Texture2D();
	Texture2D(ID3D11Device *device, UINT width, UINT height, TextureType type = TextureType::DEFAULT, TextureFormat format = TextureFormat::BGRA8_UNORM);
	~Texture2D();

	bool IsValid() const { return m_texture != nullptr; }

	// Create texture with specified parameters
	bool Create(ID3D11Device *device, UINT width, UINT height, TextureType type = TextureType::DEFAULT, TextureFormat format = TextureFormat::BGRA8_UNORM);
	bool CreateWithCustomFlags(ID3D11Device *device, UINT width, UINT height,
							   UINT bindFlags, TextureFormat format = TextureFormat::BGRA8_UNORM);
	bool CreateWithCustomFlags(ID3D11Device *device, UINT width, UINT height,
							   UINT bindFlags, UINT miscFlags, TextureFormat format = TextureFormat::BGRA8_UNORM);
	// Create from existing data
	bool CreateFromData(ID3D11Device *device, UINT width, UINT height, std::span<uint8_t> data, UINT rowPitch, TextureType type = TextureType::DEFAULT, TextureFormat format = TextureFormat::BGRA8_UNORM);

	// Resource view getter
	ID3D11ShaderResourceView *GetShaderResourceView() const;

	// Data access methods
	bool Update(ID3D11DeviceContext *context, std::span<uint8_t> data, UINT rowPitch);
	bool ReadData(ID3D11DeviceContext *context, void *data, UINT rowPitch);

	// Get dimensions
	UINT GetWidth() const { return m_width; }
	UINT GetHeight() const { return m_height; }

	// Get texture type and format
	TextureType GetType() const { return m_type; }
	TextureFormat GetFormat() const { return m_format; }

	// Clear resources
	void Release();

	// Direct resource access
	ID3D11Texture2D *GetTexture() const { return m_texture.Get(); }
	Microsoft::WRL::ComPtr<ID3D11Texture2D> GetTextureComPtr() const { return m_texture; }
	D3D11_TEXTURE2D_DESC GetDesc() const { return m_textureDesc; }

	// Helper to convert TextureFormat to DXGI_FORMAT
	static DXGI_FORMAT ConvertFormat(TextureFormat format);
	static bool CopyTexture(ID3D11DeviceContext *context, const Texture2D &srcTexture, Texture2D &dstTexture);
	static bool CopyTexture(ID3D11DeviceContext *context, const Texture2D &srcTexture, ID3D11Texture2D *pDstTexture);
	static bool CopyTexture(ID3D11DeviceContext *context, ID3D11Texture2D *pSrcTexture, Texture2D &dstTexture);

private:
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shaderResourceView;

	D3D11_TEXTURE2D_DESC m_textureDesc;
	TextureType m_type;
	TextureFormat m_format;
	UINT m_width;
	UINT m_height;
	UINT m_numChannels = 4;
};