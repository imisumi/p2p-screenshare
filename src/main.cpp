#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Ws2_32.lib")

#include "App.h"

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

int main(int, char **)
{
	try 
	{
		App app;
		app.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	catch (...)
	{
		std::cerr << "Unknown error occurred." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "Application exited successfully." << std::endl;
	// App app;
	// app.run();
	return 0;
}
// #include <windows.h>
// #include <d3d11.h>
// #include <dxgi.h>
// #include <fstream>
// #include <vector>
// #include <iostream>
// #include "nvEncodeAPI.h"

// ID3D11Device *gDevice = nullptr;
// ID3D11DeviceContext *gContext = nullptr;
// ID3D11Texture2D *gTexture = nullptr;
// void *gEncoder = nullptr;
// NV_ENCODE_API_FUNCTION_LIST nvenc = {};

// // Better error reporting
// #define CHECK_NVENC(x)                                                                                           \
// 	{                                                                                                            \
// 		NVENCSTATUS s = (x);                                                                                     \
// 		if (s != NV_ENC_SUCCESS)                                                                                 \
// 		{                                                                                                        \
// 			std::cerr << "NVENC error at line " << __LINE__ << ": 0x" << std::hex << s << std::dec << std::endl; \
// 			exit(1);                                                                                             \
// 		}                                                                                                        \
// 	}

// bool LoadNvEncApi()
// {
// 	HMODULE hModule = LoadLibraryA("nvEncodeAPI64.dll");
// 	if (!hModule)
// 		return false;
// 	using CreateFunc = NVENCSTATUS(NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST *);
// 	auto createFn = (CreateFunc)GetProcAddress(hModule, "NvEncodeAPICreateInstance");
// 	nvenc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
// 	return createFn && createFn(&nvenc) == NV_ENC_SUCCESS;
// }

// void InitD3D11(UINT width, UINT height)
// {
// 	IDXGIFactory *dxgiFactory = nullptr;
// 	CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&dxgiFactory);

// 	IDXGIAdapter *nvidiaAdapter = nullptr;
// 	for (UINT i = 0; dxgiFactory->EnumAdapters(i, &nvidiaAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
// 	{
// 		DXGI_ADAPTER_DESC desc;
// 		nvidiaAdapter->GetDesc(&desc);
// 		if (wcsstr(desc.Description, L"NVIDIA"))
// 		{
// 			std::wcout << L"Using NVIDIA GPU: " << desc.Description << std::endl;
// 			break;
// 		}
// 		nvidiaAdapter->Release();
// 		nvidiaAdapter = nullptr;
// 	}

// 	if (!nvidiaAdapter)
// 	{
// 		std::cerr << "No NVIDIA adapter found!\n";
// 		exit(1);
// 	}

// 	D3D_FEATURE_LEVEL fl;
// 	HRESULT hr = D3D11CreateDevice(
// 		nvidiaAdapter,
// 		D3D_DRIVER_TYPE_UNKNOWN,
// 		nullptr,
// 		0,
// 		nullptr,
// 		0,
// 		D3D11_SDK_VERSION,
// 		&gDevice,
// 		&fl,
// 		&gContext);

// 	nvidiaAdapter->Release();
// 	dxgiFactory->Release();

// 	if (FAILED(hr))
// 	{
// 		std::cerr << "Failed to create D3D11 device with NVIDIA adapter.\n";
// 		exit(1);
// 	}

// 	D3D11_TEXTURE2D_DESC desc = {};
// 	desc.Width = width;
// 	desc.Height = height;
// 	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
// 	desc.ArraySize = 1;
// 	desc.MipLevels = 1;
// 	desc.SampleDesc.Count = 1;
// 	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
// 	desc.Usage = D3D11_USAGE_DEFAULT;
// 	desc.CPUAccessFlags = 0;

// 	gDevice->CreateTexture2D(&desc, nullptr, &gTexture);
// }

// void FillPattern(UINT width, UINT height)
// {
// 	std::vector<unsigned char> data(width * height * 4);

// 	for (UINT y = 0; y < height; y++)
// 	{
// 		for (UINT x = 0; x < width; x++)
// 		{
// 			int index = (y * width + x) * 4;
// 			data[index + 0] = x % 255;		 // B
// 			data[index + 1] = y % 255;		 // G
// 			data[index + 2] = (x ^ y) % 255; // R
// 			data[index + 3] = 255;			 // A
// 		}
// 	}

// 	gContext->UpdateSubresource(gTexture, 0, nullptr, data.data(), width * 4, 0);
// }

// void InitNvEnc(UINT width, UINT height)
// {
// 	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS ses = {NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER};
// 	ses.device = gDevice;
// 	ses.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
// 	ses.apiVersion = NVENCAPI_VERSION;

// 	CHECK_NVENC(nvenc.nvEncOpenEncodeSessionEx(&ses, &gEncoder));

// 	// NV_ENC_INITIALIZE_PARAMS initParams = {};
// 	NV_ENC_INITIALIZE_PARAMS initParams = {NV_ENC_INITIALIZE_PARAMS_VER};
// 	NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};

// 	initParams.encodeConfig = &encodeConfig;

// 	memset(&initParams, 0, sizeof(NV_ENC_CONFIG));
// 	auto pEncodeConfig = initParams.encodeConfig;
// 	memset(&initParams, 0, sizeof(NV_ENC_INITIALIZE_PARAMS));
// 	initParams.encodeConfig = pEncodeConfig;

// 	// initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
// 	// initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
// 	// initParams.presetGUID = NV_ENC_PRESET_P3_GUID;
// 	// initParams.encodeWidth = width;
// 	// initParams.encodeHeight = height;
// 	// initParams.darWidth = width;
// 	// initParams.darHeight = height;
// 	// initParams.frameRateNum = 30;
// 	// initParams.frameRateDen = 1;
// 	// initParams.enablePTD = 1;
// 	// initParams.reportSliceOffsets = 0;
// 	// initParams.enableSubFrameWrite = 0;

// 	// // Set maxEncodeWidth and maxEncodeHeight to match encodeWidth and encodeHeight
// 	// initParams.maxEncodeWidth = width;
// 	// initParams.maxEncodeHeight = height;

// 	// initParams.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;

// 	initParams.encodeConfig->version = NV_ENC_CONFIG_VER;
// 	initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;

// 	initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
// 	initParams.presetGUID = NV_ENC_PRESET_P3_GUID;
// 	initParams.encodeWidth = width;
// 	initParams.encodeHeight = height;
// 	initParams.darWidth = width;
// 	initParams.darHeight = height;
// 	initParams.frameRateNum = 30;
// 	initParams.frameRateDen = 1;
// 	initParams.enablePTD = 1;
// 	initParams.reportSliceOffsets = 0;
// 	initParams.enableSubFrameWrite = 0;
// 	initParams.maxEncodeWidth = width;
// 	initParams.maxEncodeHeight = height;
// 	initParams.enableMEOnlyMode = false;
// 	initParams.enableOutputInVidmem = false;

// 	initParams.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;
// 	initParams.encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;

// 	NV_ENC_PRESET_CONFIG presetConfig = {NV_ENC_PRESET_CONFIG_VER, 0, {NV_ENC_CONFIG_VER}};
// 	nvenc.nvEncGetEncodePresetConfigEx(gEncoder, initParams.encodeGUID, initParams.presetGUID, NV_ENC_TUNING_INFO_HIGH_QUALITY ,&presetConfig);

// 	initParams.encodeConfig = &presetConfig.presetCfg;

// 	CHECK_NVENC(nvenc.nvEncInitializeEncoder(gEncoder, &initParams));
// }

// void EncodeTexture(UINT width, UINT height)
// {
// 	NV_ENC_REGISTER_RESOURCE reg = {NV_ENC_REGISTER_RESOURCE_VER};
// 	reg.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
// 	reg.resourceToRegister = gTexture;
// 	reg.width = width;
// 	reg.height = height;
// 	reg.bufferFormat = NV_ENC_BUFFER_FORMAT_ABGR;
// 	reg.bufferUsage = NV_ENC_INPUT_IMAGE;
// 	CHECK_NVENC(nvenc.nvEncRegisterResource(gEncoder, &reg));

// 	NV_ENC_MAP_INPUT_RESOURCE map = {NV_ENC_MAP_INPUT_RESOURCE_VER};
// 	map.registeredResource = reg.registeredResource;
// 	CHECK_NVENC(nvenc.nvEncMapInputResource(gEncoder, &map));

// 	NV_ENC_CREATE_BITSTREAM_BUFFER createBs = {NV_ENC_CREATE_BITSTREAM_BUFFER_VER};
// 	CHECK_NVENC(nvenc.nvEncCreateBitstreamBuffer(gEncoder, &createBs));

// 	NV_ENC_PIC_PARAMS pic = {NV_ENC_PIC_PARAMS_VER};
// 	pic.inputBuffer = map.mappedResource;
// 	pic.bufferFmt = NV_ENC_BUFFER_FORMAT_ABGR;
// 	pic.inputWidth = width;
// 	pic.inputHeight = height;
// 	pic.outputBitstream = createBs.bitstreamBuffer;
// 	pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
// 	CHECK_NVENC(nvenc.nvEncEncodePicture(gEncoder, &pic));

// 	NV_ENC_LOCK_BITSTREAM lock = {NV_ENC_LOCK_BITSTREAM_VER};
// 	lock.outputBitstream = createBs.bitstreamBuffer;
// 	CHECK_NVENC(nvenc.nvEncLockBitstream(gEncoder, &lock));

// 	std::ofstream file("output.h264", std::ios::binary);
// 	file.write((char *)lock.bitstreamBufferPtr, lock.bitstreamSizeInBytes);
// 	file.close();

// 	nvenc.nvEncUnlockBitstream(gEncoder, createBs.bitstreamBuffer);
// 	nvenc.nvEncUnmapInputResource(gEncoder, map.mappedResource);
// 	nvenc.nvEncUnregisterResource(gEncoder, reg.registeredResource);
// 	nvenc.nvEncDestroyBitstreamBuffer(gEncoder, createBs.bitstreamBuffer);
// }

// int main()
// {
// 	UINT width = 1920, height = 1080;

// 	if (!LoadNvEncApi())
// 	{
// 		std::cerr << "Failed to load NVENC API.\n";
// 		return -1;
// 	}

// 	InitD3D11(width, height);
// 	FillPattern(width, height);
// 	InitNvEnc(width, height);
// 	EncodeTexture(width, height);

// 	std::cout << "✅ Encoding complete! Check output.h264\n";
// 	return 0;
// }