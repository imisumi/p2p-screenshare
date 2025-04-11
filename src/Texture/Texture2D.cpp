#include "Texture2D.h"
#include <iostream>
#include <algorithm>

#include "Utils.h"

Texture2D::Texture2D()
	: m_width(0), m_height(0), m_type(TextureType::DEFAULT), m_format(TextureFormat::RGBA8_UNORM)
{
	ZeroMemory(&m_textureDesc, sizeof(m_textureDesc));
}

Texture2D::Texture2D(ID3D11Device *device, UINT width, UINT height, TextureType type, TextureFormat format)
	: m_width(0), m_height(0), m_type(TextureType::DEFAULT), m_format(TextureFormat::RGBA8_UNORM)
{
	ZeroMemory(&m_textureDesc, sizeof(m_textureDesc));
	Create(device, width, height, type, format);
}

Texture2D::~Texture2D()
{
	Release();
}

DXGI_FORMAT Texture2D::ConvertFormat(TextureFormat format)
{
	switch (format)
	{
	case TextureFormat::RGBA8_UNORM:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case TextureFormat::BGRA8_UNORM:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case TextureFormat::BGR8:
		return DXGI_FORMAT_B8G8R8X8_UNORM;
	default:
		MY_ASSERT(false, "Unsupported texture format");
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}

bool Texture2D::Create(ID3D11Device *device, UINT width, UINT height, TextureType type, TextureFormat format)
{
	MY_ASSERT(device != nullptr, "Device pointer is null");
	MY_ASSERT(width > 0 && height > 0, "Invalid texture dimensions");

	// Clear any existing resources
	Release();

	m_width = width;
	m_height = height;
	m_type = type;
	m_format = format;

	// Setup texture description
	ZeroMemory(&m_textureDesc, sizeof(m_textureDesc));
	m_textureDesc.Width = width;
	m_textureDesc.Height = height;
	m_textureDesc.MipLevels = 1;
	m_textureDesc.ArraySize = 1;
	m_textureDesc.Format = ConvertFormat(format);
	m_textureDesc.SampleDesc.Count = 1;
	m_textureDesc.SampleDesc.Quality = 0;
	m_textureDesc.Usage = D3D11_USAGE_DEFAULT;
	m_textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	m_textureDesc.CPUAccessFlags = 0;
	m_textureDesc.MiscFlags = 0;

	// Configure based on texture type
	switch (type)
	{
	case TextureType::DEFAULT:
		// Already configured with defaults
		break;

	case TextureType::DYNAMIC:
		m_textureDesc.Usage = D3D11_USAGE_DYNAMIC;
		m_textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		break;

	case TextureType::STAGING:
		m_textureDesc.Usage = D3D11_USAGE_STAGING;
		m_textureDesc.BindFlags = 0;
		m_textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		break;

	case TextureType::CPU_READ:
		m_textureDesc.Usage = D3D11_USAGE_STAGING;
		m_textureDesc.BindFlags = 0;
		m_textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		break;

	case TextureType::CPU_WRITE:
		m_textureDesc.Usage = D3D11_USAGE_DYNAMIC;
		m_textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		break;

	case TextureType::CPU_RW:
		m_textureDesc.Usage = D3D11_USAGE_STAGING;
		m_textureDesc.BindFlags = 0;
		m_textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		break;

	default:
		MY_ASSERT(false, "Unsupported texture type");
		return false;
	}

	// Create the texture
	HRESULT hr = device->CreateTexture2D(&m_textureDesc, nullptr, &m_texture);
	if (FAILED(hr))
	{
		MY_ASSERT(false, "Failed to create texture");
		return false;
	}

	// Create shader resource view if the texture is not staging
	if (m_textureDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = m_textureDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_shaderResourceView);
		if (FAILED(hr))
		{
			MY_ASSERT(false, "Failed to create shader resource view");
			return false;
		}
	}

	return true;
}

bool Texture2D::CreateWithCustomFlags(ID3D11Device *device, UINT width, UINT height,
									  UINT bindFlags, TextureFormat format)
{
	Release();

	m_width = width;
	m_height = height;
	m_format = format;
	m_type = TextureType::DEFAULT; // Custom flags override the type

	// Set up the texture description
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = ConvertFormat(format);
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = bindFlags;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	m_textureDesc = desc;

	// Create the texture
	HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_texture);
	if (FAILED(hr))
		return false;

	// Create shader resource view if needed
	if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_shaderResourceView);
		if (FAILED(hr))
			return false;
	}

	return true;
}

bool Texture2D::CreateWithCustomFlags(ID3D11Device *device, UINT width, UINT height,
	UINT bindFlags, UINT miscFlags, TextureFormat format)
{
Release();

m_width = width;
m_height = height;
m_format = format;
m_type = TextureType::DEFAULT; // Custom flags override the type

// Set up the texture description
D3D11_TEXTURE2D_DESC desc = {};
desc.Width = width;
desc.Height = height;
desc.MipLevels = 1;
desc.ArraySize = 1;
desc.Format = ConvertFormat(format);
desc.SampleDesc.Count = 1;
desc.Usage = D3D11_USAGE_DEFAULT;
desc.BindFlags = bindFlags;
desc.CPUAccessFlags = 0;
desc.MiscFlags = miscFlags;

m_textureDesc = desc;

// Create the texture
HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_texture);
if (FAILED(hr))
return false;

// Create shader resource view if needed
if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
{
D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
srvDesc.Format = desc.Format;
srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
srvDesc.Texture2D.MipLevels = 1;

hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_shaderResourceView);
if (FAILED(hr))
return false;
}

return true;
}

bool Texture2D::CreateFromData(ID3D11Device *device, UINT width, UINT height, std::span<uint8_t> data, UINT rowPitch, TextureType type, TextureFormat format)
{
	MY_ASSERT(device != nullptr, "Device pointer is null");
	MY_ASSERT(width > 0 && height > 0, "Invalid texture dimensions");
	MY_ASSERT(data.size() == width * height * 4, "Data size does not match texture dimensions");
	MY_ASSERT(data.data() != nullptr, "Data pointer is null");
	MY_ASSERT(rowPitch > 0, "Row pitch must be greater than zero");

	// Ensure that we can't create a CPU readable texture with initial data
	// as DX11 doesn't support that directly
	MY_ASSERT(type != TextureType::STAGING && type != TextureType::CPU_READ && type != TextureType::CPU_RW,
			  "Cannot create CPU readable textures with initial data");

	// Clear any existing resources
	Release();

	m_width = width;
	m_height = height;
	m_type = type;
	m_format = format;

	// Setup texture description
	ZeroMemory(&m_textureDesc, sizeof(m_textureDesc));
	m_textureDesc.Width = width;
	m_textureDesc.Height = height;
	m_textureDesc.MipLevels = 1;
	m_textureDesc.ArraySize = 1;
	m_textureDesc.Format = ConvertFormat(format);
	m_textureDesc.SampleDesc.Count = 1;
	m_textureDesc.SampleDesc.Quality = 0;
	m_textureDesc.Usage = D3D11_USAGE_DEFAULT;
	m_textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	m_textureDesc.CPUAccessFlags = 0;
	m_textureDesc.MiscFlags = 0;

	// Configure based on texture type
	switch (type)
	{
	case TextureType::DEFAULT:
		// Already configured with defaults
		break;

	case TextureType::DYNAMIC:
		m_textureDesc.Usage = D3D11_USAGE_DYNAMIC;
		m_textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		break;

	case TextureType::CPU_WRITE:
		m_textureDesc.Usage = D3D11_USAGE_DYNAMIC;
		m_textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		break;

	default:
		MY_ASSERT(false, "Unsupported texture type for CreateFromData");
		return false;
	}

	// Create subresource data
	D3D11_SUBRESOURCE_DATA subresourceData = {};
	subresourceData.pSysMem = data.data();
	subresourceData.SysMemPitch = rowPitch;
	subresourceData.SysMemSlicePitch = 0;

	// Create the texture with initial data
	HRESULT hr = device->CreateTexture2D(&m_textureDesc, &subresourceData, &m_texture);
	if (FAILED(hr))
	{
		MY_ASSERT(false, "Failed to create texture with initial data");
		return false;
	}

	// Create shader resource view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = m_textureDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_shaderResourceView);
	if (FAILED(hr))
	{
		MY_ASSERT(false, "Failed to create shader resource view");
		return false;
	}

	return true;
}

ID3D11ShaderResourceView *Texture2D::GetShaderResourceView() const
{
	MY_ASSERT(m_shaderResourceView != nullptr, "Shader resource view is null. Texture may be staging type.");
	return m_shaderResourceView.Get();
}

bool Texture2D::Update(ID3D11DeviceContext *context, std::span<uint8_t> data, UINT rowPitch)
{
	MY_ASSERT(context != nullptr, "Device context is null");
	MY_ASSERT(data.size() == m_width * m_height * 4, "Data size does not match texture dimensions");
	MY_ASSERT(data.data() != nullptr, "Data pointer is null");
	MY_ASSERT(m_texture != nullptr, "Texture is null");
	MY_ASSERT(rowPitch > 0, "Row pitch must be greater than zero");

	// Verify texture type allows writing
	MY_ASSERT(m_type == TextureType::DYNAMIC || m_type == TextureType::CPU_WRITE ||
				  m_type == TextureType::CPU_RW || m_type == TextureType::STAGING,
			  "Texture type does not support CPU writing");

	if (m_type == TextureType::DYNAMIC || m_type == TextureType::CPU_WRITE)
	{
		// Map the texture
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hr = context->Map(m_texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(hr))
		{
			MY_ASSERT(false, "Failed to map texture for writing");
			return false;
		}

		// Copy the data row by row
		const BYTE *srcData = static_cast<const BYTE *>(data.data());
		BYTE *destData = static_cast<BYTE *>(mappedResource.pData);

		for (UINT row = 0; row < m_height; ++row)
		{
			memcpy(
				destData + row * mappedResource.RowPitch,
				srcData + row * rowPitch,
				std::min(rowPitch, mappedResource.RowPitch));
		}

		// Unmap the texture
		context->Unmap(m_texture.Get(), 0);
	}
	else // STAGING or CPU_RW
	{
		// For staging textures, use UpdateSubresource
		D3D11_BOX box;
		box.left = 0;
		box.right = m_width;
		box.top = 0;
		box.bottom = m_height;
		box.front = 0;
		box.back = 1;

		context->UpdateSubresource(m_texture.Get(), 0, &box, data.data(), rowPitch, 0);
	}

	return true;
}

bool Texture2D::ReadData(ID3D11DeviceContext *context, void *data, UINT rowPitch)
{
	MY_ASSERT(context != nullptr, "Device context is null");
	MY_ASSERT(data != nullptr, "Data pointer is null");
	MY_ASSERT(m_texture != nullptr, "Texture is null");
	MY_ASSERT(rowPitch > 0, "Row pitch must be greater than zero");

	// Verify texture type allows reading
	MY_ASSERT(m_type == TextureType::CPU_READ || m_type == TextureType::CPU_RW || m_type == TextureType::STAGING,
			  "Texture type does not support CPU reading");

	// Map the texture for reading
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hr = context->Map(m_texture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
	if (FAILED(hr))
	{
		MY_ASSERT(false, "Failed to map texture for reading");
		return false;
	}

	// Copy the data row by row
	BYTE *destData = static_cast<BYTE *>(data);
	const BYTE *srcData = static_cast<const BYTE *>(mappedResource.pData);

	for (UINT row = 0; row < m_height; ++row)
	{
		memcpy(
			destData + row * rowPitch,
			srcData + row * mappedResource.RowPitch,
			std::min(rowPitch, mappedResource.RowPitch));
	}

	// Unmap the texture
	context->Unmap(m_texture.Get(), 0);

	return true;
}

void Texture2D::Release()
{
	m_shaderResourceView.Reset();
	m_texture.Reset();

	m_width = 0;
	m_height = 0;
	ZeroMemory(&m_textureDesc, sizeof(m_textureDesc));
}

// Static method to copy between textures with validation
bool Texture2D::CopyTexture(ID3D11DeviceContext* context, const Texture2D& srcTexture, Texture2D& dstTexture)
{
    MY_ASSERT(context != nullptr, "Device context is null");
    MY_ASSERT(srcTexture.GetTexture() != nullptr, "Source texture is null");
    MY_ASSERT(dstTexture.GetTexture() != nullptr, "Destination texture is null");
    
    // Check dimensions match
    MY_ASSERT(srcTexture.GetWidth() == dstTexture.GetWidth(), 
              "Texture width mismatch: src=" + std::to_string(srcTexture.GetWidth()) + 
              ", dst=" + std::to_string(dstTexture.GetWidth()));
    MY_ASSERT(srcTexture.GetHeight() == dstTexture.GetHeight(), 
              "Texture height mismatch: src=" + std::to_string(srcTexture.GetHeight()) + 
              ", dst=" + std::to_string(dstTexture.GetHeight()));
    
    // Check formats match
    MY_ASSERT(srcTexture.GetFormat() == dstTexture.GetFormat(), 
              "Texture format mismatch");
    
    // Check texture types are compatible for copying
    // Destination can't be CPU_READ only as it needs to be written to
    MY_ASSERT(dstTexture.GetType() != TextureType::CPU_READ, 
              "Destination texture type doesn't support writing");
    
    // Check MSAA settings match by comparing sample count
    MY_ASSERT(srcTexture.GetDesc().SampleDesc.Count == dstTexture.GetDesc().SampleDesc.Count,
              "Sample count mismatch");
    
    // Perform the copy
    context->CopyResource(dstTexture.GetTexture(), srcTexture.GetTexture());
    
    return true;
}

// Static method to copy between a Texture2D and a raw ID3D11Texture2D*
bool Texture2D::CopyTexture(ID3D11DeviceContext* context, const Texture2D& srcTexture, ID3D11Texture2D* pDstTexture)
{
    MY_ASSERT(context != nullptr, "Device context is null");
    MY_ASSERT(srcTexture.GetTexture() != nullptr, "Source texture is null");
    MY_ASSERT(pDstTexture != nullptr, "Destination texture is null");
    
    // Get destination texture description to check compatibility
    D3D11_TEXTURE2D_DESC dstDesc;
    pDstTexture->GetDesc(&dstDesc);
    
    // Check dimensions match
    MY_ASSERT(srcTexture.GetWidth() == dstDesc.Width, 
              "Texture width mismatch: src=" + std::to_string(srcTexture.GetWidth()) + 
              ", dst=" + std::to_string(dstDesc.Width));
    MY_ASSERT(srcTexture.GetHeight() == dstDesc.Height, 
              "Texture height mismatch: src=" + std::to_string(srcTexture.GetHeight()) + 
              ", dst=" + std::to_string(dstDesc.Height));
    
    // Check formats match
    MY_ASSERT(ConvertFormat(srcTexture.GetFormat()) == dstDesc.Format, 
              "Texture format mismatch");
    
    // Check MSAA settings match by comparing sample count
    MY_ASSERT(srcTexture.GetDesc().SampleDesc.Count == dstDesc.SampleDesc.Count,
              "Sample count mismatch");
    
    // Check destination can be written to (can't be read-only)
    MY_ASSERT(!(dstDesc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) || (dstDesc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE),
              "Destination texture doesn't support writing");
    
    // Perform the copy
    context->CopyResource(pDstTexture, srcTexture.GetTexture());
    
    return true;
}

// Overload for the reverse direction (raw pointer to Texture2D)
bool Texture2D::CopyTexture(ID3D11DeviceContext* context, ID3D11Texture2D* pSrcTexture, Texture2D& dstTexture)
{
    MY_ASSERT(context != nullptr, "Device context is null");
    MY_ASSERT(pSrcTexture != nullptr, "Source texture is null");
    MY_ASSERT(dstTexture.GetTexture() != nullptr, "Destination texture is null");
    
    // Get source texture description to check compatibility
    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcTexture->GetDesc(&srcDesc);
    
    // Check dimensions match
    MY_ASSERT(srcDesc.Width == dstTexture.GetWidth(), 
              "Texture width mismatch: src=" + std::to_string(srcDesc.Width) + 
              ", dst=" + std::to_string(dstTexture.GetWidth()));
    MY_ASSERT(srcDesc.Height == dstTexture.GetHeight(), 
              "Texture height mismatch: src=" + std::to_string(srcDesc.Height) + 
              ", dst=" + std::to_string(dstTexture.GetHeight()));
    
    // Check formats match
    MY_ASSERT(srcDesc.Format == ConvertFormat(dstTexture.GetFormat()), 
              "Texture format mismatch");
    
    // Check MSAA settings match by comparing sample count
    MY_ASSERT(srcDesc.SampleDesc.Count == dstTexture.GetDesc().SampleDesc.Count,
              "Sample count mismatch");
    
    // Check destination can be written to
    MY_ASSERT(dstTexture.GetType() != TextureType::CPU_READ, 
              "Destination texture type doesn't support writing");
    
    // Perform the copy
    context->CopyResource(dstTexture.GetTexture(), pSrcTexture);
    
    return true;
}