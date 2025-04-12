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
