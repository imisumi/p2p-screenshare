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


#include "AppEncD3D11.h"

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

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
	// std::vector<uint8_t> colorData = coolPattern2(nWidth, nHeight);

	// if (!SetDataToStagingTexture(pContext, pTexSysMem, colorData, nHeight, nWidth))
	// {
	// 	throw std::runtime_error("Failed to set data to staging texture");
	// }
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
