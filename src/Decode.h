#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>
// #include "nvcodec/NvDecoder/NvDecoder.h"
#include "Codec/NvCodec/NvDecoder/NvDecoder.h"

using Microsoft::WRL::ComPtr;

class NvDecToDX11 {
private:
    // DirectX 11 references (not owned by this class)
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    
    // Output texture (owned by this class)
    ComPtr<ID3D11Texture2D> outputTexture;
    
    // CUDA objects
    CUcontext cuContext = nullptr;
    CUdevice cuDevice = 0;
    
    // NVDEC decoder
    NvDecoder* decoder = nullptr;
    
    // Frame dimensions
    int width = 0;
    int height = 0;
    
    // CUDA-DirectX interop resources
    cudaGraphicsResource_t cudaResource = nullptr;
    
public:
    NvDecToDX11(ID3D11Device* device, ID3D11DeviceContext* context)
        : d3dDevice(device), d3dContext(context) {
    }
    
    ~NvDecToDX11() {
        Cleanup();
    }
    
    bool Initialize() {
        if (!d3dDevice || !d3dContext) {
            std::cerr << "DirectX 11 device or context is null" << std::endl;
            return false;
        }
        
        // Initialize CUDA
        cuInit(0);
        
        // Create a CUDA context
        if (CreateCudaContext() != 0) {
            std::cerr << "Failed to create CUDA context" << std::endl;
            return false;
        }
        
        return true;
    }
    
    int CreateCudaContext() {
        // Get the DXGI adapter associated with the D3D device
        IDXGIDevice* dxgiDevice = nullptr;
        HRESULT hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (FAILED(hr)) {
            std::cerr << "Failed to get DXGI device" << std::endl;
            return -1;
        }
        
        IDXGIAdapter* dxgiAdapter = nullptr;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        dxgiDevice->Release();
        
        if (FAILED(hr)) {
            std::cerr << "Failed to get DXGI adapter" << std::endl;
            return -1;
        }
        
        // Get the adapter LUID
        DXGI_ADAPTER_DESC adapterDesc;
        dxgiAdapter->GetDesc(&adapterDesc);
        dxgiAdapter->Release();
        
        // Find the matching CUDA device
        int deviceCount = 0;
        cuDeviceGetCount(&deviceCount);
        
        for (int i = 0; i < deviceCount; i++) {
            CUdevice device;
            cuDeviceGet(&device, i);
            
            // Use UUID to match the device
            CUuuid deviceUUID;
            cuDeviceGetUuid(&deviceUUID, device);
            
            // Match with the DXGI adapter LUID
            // This matching logic varies depending on your NVIDIA driver version
            // For newer drivers, you might use cuDeviceGetLuid
            
            // For this example, we'll just take the first CUDA device
            // In a real application, proper matching is important
            cuDevice = device;
            break;
        }
        
        // Create the CUDA context
        CUresult cuResult = cuCtxCreate(&cuContext, CU_CTX_SCHED_BLOCKING_SYNC, cuDevice);
        if (cuResult != CUDA_SUCCESS) {
            std::cerr << "Failed to create CUDA context: " << cuResult << std::endl;
            return -1;
        }
        
        return 0;
    }
    
    bool CreateDecoder(int codecId = cudaVideoCodec_H264) {
        if (!cuContext) {
            std::cerr << "CUDA context not initialized" << std::endl;
            return false;
        }
        
        try {
            // Create NVDEC decoder with explicit cast to cudaVideoCodec enum
            decoder = new NvDecoder(cuContext, true, static_cast<cudaVideoCodec>(codecId), false, false);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to create decoder: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool CreateOutputTexture(int width, int height) {
        this->width = width;
        this->height = height;
        
        // Create a texture for the decoded frames
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_NV12; // NV12 is commonly used for video
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        texDesc.CPUAccessFlags = 0;
        texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        
        HRESULT hr = d3dDevice->CreateTexture2D(&texDesc, nullptr, outputTexture.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "Failed to create output texture" << std::endl;
            return false;
        }
        
        // Register the texture with CUDA
        cudaError_t cudaStatus = cudaGraphicsD3D11RegisterResource(
            &cudaResource,
            outputTexture.Get(),
            cudaGraphicsRegisterFlagsWriteDiscard
        );
        
        if (cudaStatus != cudaSuccess) {
            std::cerr << "Failed to register D3D resource with CUDA: " 
                      << cudaGetErrorString(cudaStatus) << std::endl;
            return false;
        }
        
        return true;
    }
    
    bool DecodeFrame(const std::vector<uint8_t>& bitstream) {
        if (!decoder) {
            std::cerr << "Decoder not initialized" << std::endl;
            return false;
        }
        
        if (bitstream.empty()) {
            std::cerr << "Empty bitstream" << std::endl;
            return false;
        }
        
        // Decode the frame
        // Fix: Call Decode with the correct parameter types according to the API
        int numFramesDecoded = 0;
        
        try {
            // Decode method signature in NvDecoder: int Decode(const uint8_t *pData, int nSize, int nFlags = 0, int64_t nTimestamp = 0)
            numFramesDecoded = decoder->Decode(bitstream.data(), static_cast<int>(bitstream.size()), 0, 0);
            
            if (numFramesDecoded == 0) {
                // No frames were decoded, this is normal for some packets
                return true;
            }
            
            // Get the decoded frame - note that we get frames directly from GetFrame now
            int64_t timestamp = 0;
            uint8_t* decodedFrame = decoder->GetFrame(&timestamp);
            
            if (!decodedFrame) {
                std::cerr << "No decoded frame available" << std::endl;
                return false;
            }
            
            // If this is the first frame, create the output texture with the right dimensions
            if (width == 0 || height == 0) {
                width = decoder->GetWidth();
                height = decoder->GetHeight();
                
                if (!CreateOutputTexture(width, height)) {
                    std::cerr << "Failed to create output texture" << std::endl;
                    return false;
                }
            }
            
            // Copy the decoded frame to the DX11 texture
            return CopyDecodedFrameToTexture(decodedFrame);
        }
        catch (const std::exception& e) {
            std::cerr << "Error during decoding: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool CopyDecodedFrameToTexture(const uint8_t* decodedFrame) {
        if (!decodedFrame || !cudaResource) {
            return false;
        }
        
        // Map the resource for CUDA access
        cudaError_t cudaStatus = cudaGraphicsMapResources(1, &cudaResource);
        if (cudaStatus != cudaSuccess) {
            std::cerr << "Failed to map resource: " << cudaGetErrorString(cudaStatus) << std::endl;
            return false;
        }
        
        // Get the mapped resource pointer
        cudaArray_t mappedArray;
        cudaStatus = cudaGraphicsSubResourceGetMappedArray(&mappedArray, cudaResource, 0, 0);
        if (cudaStatus != cudaSuccess) {
            std::cerr << "Failed to get mapped array: " << cudaGetErrorString(cudaStatus) << std::endl;
            cudaGraphicsUnmapResources(1, &cudaResource);
            return false;
        }
        
        // Calculate the pitch of the NV12 frame
        size_t pitch = decoder->GetDeviceFramePitch();
        
        // Copy the data to the texture (Y plane)
        CUDA_MEMCPY2D copyParams = {};
        copyParams.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copyParams.srcDevice = (CUdeviceptr)decodedFrame;
        copyParams.srcPitch = pitch;
        copyParams.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        // Fix: Cast cudaArray_t to CUarray with reinterpret_cast
        copyParams.dstArray = reinterpret_cast<CUarray>(mappedArray);
        copyParams.WidthInBytes = width;
        copyParams.Height = height;
        
        CUresult result = cuMemcpy2D(&copyParams);
        if (result != CUDA_SUCCESS) {
            std::cerr << "Failed to copy Y plane: " << result << std::endl;
            cudaGraphicsUnmapResources(1, &cudaResource);
            return false;
        }
        
        // Copy the UV plane (for NV12 format)
        // UV plane starts after the Y plane
        copyParams.srcDevice = (CUdeviceptr)(decodedFrame + pitch * height);
        // Fix: Cast cudaArray_t to CUarray with reinterpret_cast again
        copyParams.dstArray = reinterpret_cast<CUarray>(mappedArray);
        copyParams.WidthInBytes = width;
        copyParams.Height = height / 2;  // UV plane is half the height
        
        result = cuMemcpy2D(&copyParams);
        if (result != CUDA_SUCCESS) {
            std::cerr << "Failed to copy UV plane: " << result << std::endl;
            cudaGraphicsUnmapResources(1, &cudaResource);
            return false;
        }
        
        // Unmap the resource
        cudaStatus = cudaGraphicsUnmapResources(1, &cudaResource);
        if (cudaStatus != cudaSuccess) {
            std::cerr << "Failed to unmap resource: " << cudaGetErrorString(cudaStatus) << std::endl;
            return false;
        }
        
        return true;
    }
    
    ID3D11Texture2D* GetOutputTexture() const {
        return outputTexture.Get();
    }
    
    void Cleanup() {
        // Unregister CUDA resource
        if (cudaResource) {
            cudaGraphicsUnregisterResource(cudaResource);
            cudaResource = nullptr;
        }
        
        // Delete the decoder
        if (decoder) {
            delete decoder;
            decoder = nullptr;
        }
        
        // Destroy CUDA context
        if (cuContext) {
            cuCtxDestroy(cuContext);
            cuContext = nullptr;
        }
        
        // Release DirectX resources
        outputTexture.Reset();
    }
};

// Example usage for screen sharing application
class ScreenSharingDecoder {
private:
    ID3D11Device* d3dDevice;
    ID3D11DeviceContext* d3dContext;
    std::unique_ptr<NvDecToDX11> decoder;
    
public:
    ScreenSharingDecoder(ID3D11Device* device, ID3D11DeviceContext* context) 
        : d3dDevice(device), d3dContext(context) {
        
        // Create the decoder with existing DX11 device
        decoder = std::make_unique<NvDecToDX11>(d3dDevice, d3dContext);
        
        if (!decoder->Initialize()) {
            throw std::runtime_error("Failed to initialize decoder");
        }
        
        if (!decoder->CreateDecoder()) {
            throw std::runtime_error("Failed to create NVDEC decoder");
        }
    }
    
    // Call this method when you receive a new frame from the network
    bool ProcessNetworkFrame(const std::vector<uint8_t>& frameData) {
        if (frameData.empty()) {
            return false;
        }
        
        // Decode the received H.264 frame
        bool success = decoder->DecodeFrame(frameData);
        
        if (success) {
            // At this point, the decoded frame is in the DirectX texture
            // You can use it for rendering or further processing
            ID3D11Texture2D* decodedTexture = decoder->GetOutputTexture();
            
            // Your rendering code here...
            // Example:
            // d3dContext->PSSetShaderResources(0, 1, &shaderResourceView);
            // d3dContext->Draw(...);
            
            return true;
        }
        
        return false;
    }
    
    ID3D11Texture2D* GetDecodedTexture() const {
        return decoder->GetOutputTexture();
    }
};

// // Example of integration with a network-based screen sharing app
// void ScreenSharingExample(ID3D11Device* existingDevice, ID3D11DeviceContext* existingContext) {
//     try {
//         // Set up the decoder
//         ScreenSharingDecoder screenDecoder(existingDevice, existingContext);
        
//         // Your network receiving code
//         NetworkReceiver receiver;  // Hypothetical network class
        
//         // Main application loop
//         while (applicationRunning) {
//             // Check for new frames from network
//             if (receiver.HasNewFrame()) {
//                 // Get the compressed H.264 frame from network
//                 std::vector<uint8_t> compressedFrame = receiver.GetNextFrame();
                
//                 // Process the frame (decode it to DX11 texture)
//                 if (screenDecoder.ProcessNetworkFrame(compressedFrame)) {
//                     // Frame was successfully decoded
                    
//                     // Get the decoded texture for rendering
//                     ID3D11Texture2D* texture = screenDecoder.GetDecodedTexture();
                    
//                     // Render the texture to screen
//                     RenderTextureToScreen(texture);  // Your rendering function
//                 }
//             }
            
//             // Handle other application logic
//             ProcessEvents();
            
//             // Render your UI and other elements
//             RenderUI();
            
//             // Present the frame
//             existingContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
//             swapChain->Present(1, 0);
//         }
//     }
//     catch (const std::exception& e) {
//         std::cerr << "Error in screen sharing: " << e.what() << std::endl;
//     }
// }