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

#pragma once
#include <iostream>
#include "Codec/NvCodec/NvEncoder/NvEncoder.h"
#include "Codec/Utils/NvEncoderCLIOptions.h"

inline void ShowHelpAndExit_AppEncD3D(const char *szBadOption = NULL, bool bOutputInVidMem = false, bool bIsD3D12Encode = false)
{
	bool bThrowError = false;
	std::ostringstream oss;
	if (szBadOption)
	{
		bThrowError = true;
		oss << "Error parsing \"" << szBadOption << "\"" << std::endl;
	}
	oss << "Options:" << std::endl
		<< "-i           Input file (must be in BGRA format) path" << std::endl
		<< "-o           Output file path" << std::endl
		<< "-s           Input resolution in this form: WxH" << std::endl
		<< "-gpu         Ordinal of GPU to use" << std::endl;

	oss << NvEncoderInitParam().GetHelpMessage(false, false, bIsD3D12Encode ? true : false, false, false, !bIsD3D12Encode, bIsD3D12Encode);
	if (bThrowError)
	{
		throw std::invalid_argument(oss.str());
	}
	else
	{
		std::cout << oss.str();
		exit(0);
	}
}

inline void ParseCommandLine_AppEncD3D(int argc, const char *argv[], int &nWidth, int &nHeight,
									   NvEncoderInitParam &initParam, int &iGpu, bool bEnableOutputInVidMem = false, bool bIsD3D12Encode = false)
{
	std::ostringstream oss;
	int i;
	for (i = 0; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-h"))
		{
			ShowHelpAndExit_AppEncD3D(NULL, bEnableOutputInVidMem, bIsD3D12Encode);
		}
		if (!_stricmp(argv[i], "-s"))
		{
			if (++i == argc || 2 != sscanf(argv[i], "%dx%d", &nWidth, &nHeight))
			{
				ShowHelpAndExit_AppEncD3D("-s", bEnableOutputInVidMem, bIsD3D12Encode);
			}
			continue;
		}
		if (!_stricmp(argv[i], "-gpu"))
		{
			if (++i == argc)
			{
				ShowHelpAndExit_AppEncD3D("-gpu", bEnableOutputInVidMem, bIsD3D12Encode);
			}
			iGpu = atoi(argv[i]);
			continue;
		}
		if (!_stricmp(argv[i], "-outputInVidMem"))
		{
			if (++i == argc)
			{
				ShowHelpAndExit_AppEncD3D("-outputInVidMem", bEnableOutputInVidMem, bIsD3D12Encode);
			}
			// if (outputInVidMem != NULL)
			// {
			//     *outputInVidMem = (atoi(argv[i]) != 0) ? 1 : 0;
			// }
			continue;
		}

		// Regard as encoder parameter
		if (argv[i][0] != '-')
		{
			ShowHelpAndExit_AppEncD3D(argv[i], bEnableOutputInVidMem, bIsD3D12Encode);
		}
		oss << argv[i] << " ";
		while (i + 1 < argc && argv[i + 1][0] != '-')
		{
			oss << argv[++i] << " ";
		}
	}
	initParam = NvEncoderInitParam(oss.str().c_str());
}
