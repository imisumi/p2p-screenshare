/*
 * Copyright 2017-2024 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

/**
 *  This sample application illustrates encoding of frames in ID3D11Texture2D textures.
 *  There are 2 modes of operation demonstrated in this application.
 *  In the default mode application reads RGB data from file and copies it to D3D11 textures
 *  obtained from the encoder using NvEncoder::GetNextInputFrame() and the RGB texture is
 *  submitted to NVENC for encoding. In the second case ("-nv12" option) the application converts
 *  RGB textures to NV12 textures using DXVA's VideoProcessBlt API call and the NV12 texture is
 *  submitted for encoding.
 *
 *  This sample application also illustrates the use of video memory buffer allocated
 *  by the application to get the NVENC hardware output. This feature can be used
 *  for H264 ME-only mode, H264 encode, HEVC encode and AV1 encode.
 */

#include <d3d11.h>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <wrl.h>
#include "../NvCodec/NvEncoder/NvEncoderD3D11.h"
// #include "../NvCodec/NvEncoder/NvEncoderOutputInVidMemD3D11.h"
#include "../Utils/Logger.h"
#include "../Utils/NvCodecUtils.h"
#include "Common/AppEncUtils.h"
#include "Common/AppEncUtilsD3D11.h"

using Microsoft::WRL::ComPtr;

template <class EncoderClass>
void InitializeEncoder(EncoderClass &enc, NvEncoderInitParam encodeCLIOptions, NV_ENC_BUFFER_FORMAT eFormat)
{
	NV_ENC_INITIALIZE_PARAMS initializeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
	NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};

	initializeParams.encodeConfig = &encodeConfig;
	enc.CreateDefaultEncoderParams(&initializeParams, encodeCLIOptions.GetEncodeGUID(), encodeCLIOptions.GetPresetGUID(), encodeCLIOptions.GetTuningInfo());

	encodeCLIOptions.SetInitParams(&initializeParams, eFormat);

	enc.CreateEncoder(&initializeParams);
}

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

std::vector<uint8_t> coolPattern1(int nWidth, int nHeight)
{
	std::vector<uint8_t> colorData;
	colorData.reserve(nWidth * nHeight * 4);

	for (int y = 0; y < nHeight; y++)
	{
		for (int x = 0; x < nWidth; x++)
		{
			// Calculate normalized coordinates (0.0 to 1.0)
			float nx = static_cast<float>(x) / nWidth;
			float ny = static_cast<float>(y) / nHeight;

			// Base gradient - blue to purple
			uint8_t r = static_cast<uint8_t>(nx * 128 + ny * 64);
			uint8_t g = static_cast<uint8_t>(nx * ny * 255);
			uint8_t b = static_cast<uint8_t>(200 + nx * 55);
			uint8_t a = 255; // Full opacity

			// Add circular pattern
			float cx = x - nWidth * 0.5f;
			float cy = y - nHeight * 0.5f;
			float distance = sqrt(cx * cx + cy * cy) / (nWidth * 0.5f);

			// Rings
			if (fmod(distance * 10, 1.0f) > 0.5f)
			{
				r = 255 - r;
				g = 255 - g;
			}

			// Radial lines
			float angle = atan2(cy, cx);
			if (fmod(angle + distance, 0.5f) < 0.1f)
			{
				r = 255;
				g = 255;
				b = 220;
			}

			// Checkerboard pattern in corners
			if ((x < nWidth / 4 || x > nWidth * 3 / 4) && (y < nHeight / 4 || y > nHeight * 3 / 4))
			{
				if (((x / 20) + (y / 20)) % 2 == 0)
				{
					r = 255 - r;
					g = 255 - g;
					b = 255 - b;
				}
			}

			// Add to vector (BGRA order)
			colorData.push_back(b);
			colorData.push_back(g);
			colorData.push_back(r);
			colorData.push_back(a);
		}
	}
	return colorData;
}

std::vector<uint8_t> coolPattern2(int nWidth, int nHeight)
{
	std::vector<uint8_t> colorData;
	colorData.reserve(nWidth * nHeight * 4);
	float centerX = nWidth / 2.0f;
	float centerY = nHeight / 2.0f;

	const FLOAT M_PI = 3.14159265358979323846f;

	for (int y = 0; y < nHeight; y++)
	{
		for (int x = 0; x < nWidth; x++)
		{
			// Calculate position relative to center
			float dx = (x - centerX) / centerX;
			float dy = (y - centerY) / centerY;

			// Calculate polar coordinates
			float radius = sqrt(dx * dx + dy * dy);
			float angle = atan2(dy, dx);

			// Create a kaleidoscope effect with 8 segments
			float segmentAngle = std::fmod(angle + 2 * M_PI, 2 * M_PI / 8) * 4;

			// Create fractal-like patterns based on radius
			float radiusEffect = sin(radius * 20) * 0.5f + 0.5f;
			float patternMix = sin(radius * 10 + segmentAngle * 3) * 0.5f + 0.5f;

			// Create color based on these effects
			uint8_t r = static_cast<uint8_t>(255 * (sin(segmentAngle + radiusEffect * 3) * 0.5f + 0.5f));
			uint8_t g = static_cast<uint8_t>(255 * (sin(segmentAngle * 2 + radius * 5) * 0.5f + 0.5f));
			uint8_t b = static_cast<uint8_t>(255 * (cos(radius * 15 + patternMix) * 0.5f + 0.5f));

			// Add swirl effect
			if (radius < 0.8f)
			{
				float swirl = sin(radius * 20 + angle * 8) * 0.5f + 0.5f;
				r = static_cast<uint8_t>(r * (1.0f - radius) + swirl * 255 * radius);
				g = static_cast<uint8_t>(g * (1.0f - radius) + swirl * 128 * radius);
			}

			// Add rings
			if (fmod(radius * 12, 1.0f) > 0.5f)
			{
				r = 255 - r;
				b = 255 - b;
			}

			// Add to vector (BGRA order)
			colorData.push_back(b);
			colorData.push_back(g);
			colorData.push_back(r);
			colorData.push_back(255); // Alpha
		}
	}
	return colorData;
}

// Function to set data to a staging texture
bool SetDataToStagingTexture(ID3D11DeviceContext *pContext, ID3D11Texture2D *pTexSysMem,
							 const std::vector<uint8_t> &colorData, int nHeight, int nWidth)
{
	int nSize = nWidth * nHeight * 4; // 4 bytes per pixel (BGRA)

	// Check if vector has enough data
	if (colorData.size() < nSize)
	{
		std::cout << "Error: Vector size is too small. Expected " << nSize << " bytes, got " << colorData.size() << std::endl;
		return false;
	}

	// Map the texture for writing
	D3D11_MAPPED_SUBRESOURCE map;
	ck(pContext->Map(pTexSysMem, D3D11CalcSubresource(0, 0, 1), D3D11_MAP_WRITE, 0, &map));

	// Copy each row from vector to texture
	for (int y = 0; y < nHeight; y++)
	{
		memcpy((uint8_t *)map.pData + y * map.RowPitch,
			   colorData.data() + y * nWidth * 4,
			   nWidth * 4);
	}

	// Unmap the texture
	pContext->Unmap(pTexSysMem, D3D11CalcSubresource(0, 0, 1));

	return true;
}

// Function to copy from staging texture to encoder input frame
void CopyToEncoderInputFrame(ID3D11DeviceContext *pContext, ID3D11Texture2D *pTexSysMem,
							 const NvEncInputFrame *encoderInputFrame)
{
	ID3D11Texture2D *pTexBgra = reinterpret_cast<ID3D11Texture2D *>(encoderInputFrame->inputPtr);
	pContext->CopyResource(pTexBgra, pTexSysMem);
}

std::vector<uint8_t> Encode(NvEncoderD3D11 &enc)
{
	std::vector<NvEncOutputFrame> vPacket;

	std::vector<uint8_t> output;
	{
		auto start = std::chrono::high_resolution_clock::now();

		std::cout << "EncodeFrame" << std::endl;
		enc.EncodeFrame(vPacket);
		std::cout << "Time taken: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() << "ms" << std::endl;

		std::cout << "Packet size: " << vPacket.size() << std::endl;

		for (NvEncOutputFrame &packet : vPacket)
		{
			std::cout << "Writing to file" << std::endl;
			output.insert(output.end(), packet.frame.begin(), packet.frame.end());
		}
	}

	return output;
}

void PrepEncode(ID3D11DeviceContext *pContext, int nWidth, int nHeight,
				ID3D11Texture2D *pTexSysMem, NvEncoderD3D11 &enc)
{
	const NvEncInputFrame *encoderInputFrame = enc.GetNextInputFrame();

	// std::vector<uint8_t> colorData = coolPattern1(nWidth, nHeight);
	std::vector<uint8_t> colorData = coolPattern2(nWidth, nHeight);

	if (!SetDataToStagingTexture(pContext, pTexSysMem, colorData, nHeight, nWidth))
	{
		throw std::runtime_error("Failed to set data to staging texture");
	}
	CopyToEncoderInputFrame(pContext, pTexSysMem, encoderInputFrame);

	//? output to file
	std::ofstream outFile("output.h264", std::ios::out | std::ios::binary);
	if (!outFile)
	{
		throw std::runtime_error("Failed to open output file");
	}

	{
		std::vector<uint8_t> output = Encode(enc);
		outFile.write(reinterpret_cast<char *>(output.data()), output.size());
	}

	enc.DestroyEncoder();
}

int initEncoder(int argc, const char **argv)
{
	int nWidth = 0, nHeight = 0;
	try
	{
		NvEncoderInitParam encodeCLIOptions;
		int iGpu = 0;

		ParseCommandLine_AppEncD3D(argc, argv, nWidth, nHeight, encodeCLIOptions, iGpu, true);

		ValidateResolution(nWidth, nHeight);

		ComPtr<ID3D11Device> pDevice;
		ComPtr<ID3D11DeviceContext> pContext;
		ComPtr<IDXGIFactory1> pFactory;
		ComPtr<IDXGIAdapter> pAdapter;

		ck(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)pFactory.GetAddressOf()));
		ck(pFactory->EnumAdapters(iGpu, pAdapter.GetAddressOf()));

		ck(D3D11CreateDevice(pAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
							 NULL, 0, D3D11_SDK_VERSION, pDevice.GetAddressOf(), NULL, pContext.GetAddressOf()));

		DXGI_ADAPTER_DESC adapterDesc;
		pAdapter->GetDesc(&adapterDesc);
		char szDesc[80];
		wcstombs(szDesc, adapterDesc.Description, sizeof(szDesc));
		std::cout << "GPU in use: " << szDesc << std::endl;

		ComPtr<ID3D11Texture2D> pTexSysMem;
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
		desc.Width = nWidth;
		desc.Height = nHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ck(pDevice->CreateTexture2D(&desc, NULL, pTexSysMem.GetAddressOf()));

		//? NV_ENC_BUFFER_FORMAT_ARGB is good since its in reverse order of BGRA
		NvEncoderD3D11 enc(pDevice.Get(), nWidth, nHeight, NV_ENC_BUFFER_FORMAT_ARGB);
		InitializeEncoder(enc, encodeCLIOptions, NV_ENC_BUFFER_FORMAT_ARGB);

		PrepEncode(pContext.Get(), nWidth, nHeight,
				   pTexSysMem.Get(), enc);
	}
	catch (const std::exception &ex)
	{
		std::cout << ex.what();
		exit(1);
	}
	return 0;
}

#if 0
int main(int argc, char **argv)
{
	char szOutFilePath[256] = "out.h264";
	int nWidth = 0, nHeight = 0;
	try
	{
		NvEncoderInitParam encodeCLIOptions;
		int iGpu = 0;

		ParseCommandLine_AppEncD3D(argc, argv, nWidth, nHeight, szOutFilePath, encodeCLIOptions, iGpu, true);

		// output files
		std::ofstream fpOut(szOutFilePath, std::ios::out | std::ios::binary);
		if (!fpOut)
		{
			std::ostringstream err;
			err << "Unable to open output file: " << szOutFilePath << std::endl;
			throw std::invalid_argument(err.str());
		}

		ValidateResolution(nWidth, nHeight);

		ComPtr<ID3D11Device> pDevice;
		ComPtr<ID3D11DeviceContext> pContext;
		ComPtr<IDXGIFactory1> pFactory;
		ComPtr<IDXGIAdapter> pAdapter;

		ck(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)pFactory.GetAddressOf()));
		ck(pFactory->EnumAdapters(iGpu, pAdapter.GetAddressOf()));

		ck(D3D11CreateDevice(pAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
							 NULL, 0, D3D11_SDK_VERSION, pDevice.GetAddressOf(), NULL, pContext.GetAddressOf()));

		DXGI_ADAPTER_DESC adapterDesc;
		pAdapter->GetDesc(&adapterDesc);
		char szDesc[80];
		wcstombs(szDesc, adapterDesc.Description, sizeof(szDesc));
		std::cout << "GPU in use: " << szDesc << std::endl;

		ComPtr<ID3D11Texture2D> pTexSysMem;
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
		desc.Width = nWidth;
		desc.Height = nHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ck(pDevice->CreateTexture2D(&desc, NULL, pTexSysMem.GetAddressOf()));

		Encode(pDevice.Get(), pContext.Get(), nWidth, nHeight, encodeCLIOptions,
			   pTexSysMem.Get(), fpOut);

		std::cout << "Saved in file " << szOutFilePath << std::endl;
	}
	catch (const std::exception &ex)
	{
		std::cout << ex.what();
		exit(1);
	}
	return 0;
}
#endif
