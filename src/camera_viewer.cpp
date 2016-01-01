#include <windows.h>
#include "pxcsensemanager.h"
#include "pxcprojection.h"
#include "pxcmetadata.h"
#include "util_cmdline.h"
#include "util_render.h"
#include <conio.h>
#include <string.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <strsafe.h>

//Code adapted from the Kinect v2 samples
HRESULT SaveBitmapToFile(BYTE* pBitmapBits, LONG lWidth, LONG lHeight, WORD wBitsPerPixel, LPCWSTR lpszFilePath)
{
    DWORD dwByteCount = lWidth * lHeight * (wBitsPerPixel / 8);

    BITMAPINFOHEADER bmpInfoHeader = {0};

    bmpInfoHeader.biSize        = sizeof(BITMAPINFOHEADER);  // Size of the header
    bmpInfoHeader.biBitCount    = wBitsPerPixel;             // Bit count
    bmpInfoHeader.biCompression = BI_RGB;                    // Standard RGB, no compression
    bmpInfoHeader.biWidth       = lWidth;                    // Width in pixels
    bmpInfoHeader.biHeight      = -lHeight;                  // Height in pixels, negative indicates it's stored right-side-up
    bmpInfoHeader.biPlanes      = 1;                         // Default
    bmpInfoHeader.biSizeImage   = dwByteCount;               // Image size in bytes

    BITMAPFILEHEADER bfh = {0};

    bfh.bfType    = 0x4D42;                                           // 'M''B', indicates bitmap
    bfh.bfOffBits = bmpInfoHeader.biSize + sizeof(BITMAPFILEHEADER);  // Offset to the start of pixel data
    bfh.bfSize    = bfh.bfOffBits + bmpInfoHeader.biSizeImage;        // Size of image + headers

    // Create the file on disk to write to
    HANDLE hFile = CreateFileW(lpszFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    // Return if error opening file
    if (NULL == hFile) 
    {
        return E_ACCESSDENIED;
    }

    DWORD dwBytesWritten = 0;
    // Write the bitmap file header
    if (!WriteFile(hFile, &bfh, sizeof(bfh), &dwBytesWritten, NULL))
    {
        CloseHandle(hFile);
        return E_FAIL;
    }
    
    // Write the bitmap info header
    if (!WriteFile(hFile, &bmpInfoHeader, sizeof(bmpInfoHeader), &dwBytesWritten, NULL))
    {
        CloseHandle(hFile);
        return E_FAIL;
    }
    
    // Write the RGB Data
    if (!WriteFile(hFile, pBitmapBits, bmpInfoHeader.biSizeImage, &dwBytesWritten, NULL))
    {
        CloseHandle(hFile);
        return E_FAIL;
    }    

    // Close the file
    CloseHandle(hFile);
    return S_OK;
}


//https://software.intel.com/sites/landingpage/realsense/camera-sdk/v1.1/documentation/html/index.html?doc_essential_strong_synchronization.html
int wmain(int argc, WCHAR* argv[]) {
	int NFrames = 400;
	if (argc > 1) {
		NFrames = _wtoi(argv[1]);
	}
	std::cout << "Capturing " << NFrames << " frames";
	int width = 640, height = 480;
	BYTE* colorFrames = new BYTE[NFrames*width*height*3];
	float* depthFrames = new float[NFrames*width*height];
	PXCPointF32* UVMaps = new PXCPointF32[NFrames*width*height];

	PXCSenseManager* sm=PXCSenseManager::CreateInstance();
	PXCSession* session = sm->QuerySession();
	

	// Select the color and depth streams
	sm->EnableStream(PXCCapture::STREAM_TYPE_COLOR, width, height, 60.0f);
	sm->EnableStream(PXCCapture::STREAM_TYPE_DEPTH, width, height, 60.0f);
	

	// Initialize and Stream Samples
	sm->Init(); 

	//Get projection operator
	PXCCaptureManager* captureManager = sm->QueryCaptureManager();	
	PXCCapture::Device* device = captureManager->QueryDevice();
	PXCProjection* projection = device->CreateProjection();

	std::ofstream fout;
	fout.open("B-timings.txt");
	fout << "n, color, depth\n";
	//Step 1: Throw away the first 50 frames because the camera seems to be warming up
	for (int i = 0; i < 50; i++) {
		if (sm->AcquireFrame(true)<PXC_STATUS_NO_ERROR) break; 
		sm->ReleaseFrame(); 
	}
	//Step 2: Capture frames
	for (int i = 0; i < NFrames; i++) {
		std::cout << "Capturing frame " << i << "\n";
		fout << i << ", ";
		// This function blocks until both samples are ready

		if (sm->AcquireFrame(true)<PXC_STATUS_NO_ERROR) break; 

		//Retrieve the color and depth samples aligned
		PXCCapture::Sample *sample=sm->QuerySample();

		//Capture the color frame
		PXCImage* c = sample->color;
		pxcI64 ctimestamp = c->QueryTimeStamp();
		fout << ctimestamp << ", ";
		PXCImage::ImageData imageData;
		//https://software.intel.com/sites/landingpage/realsense/camera-sdk/v1.1/documentation/html/manuals_image_and_audio_data.html
		c->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB24, &imageData);
		memcpy(&colorFrames[i*width*height*3], imageData.planes[0], sizeof(BYTE)*width*height*3);
		c->ReleaseAccess(&imageData);

		//Capture the depth frame
		PXCImage* d = sample->depth;
		pxcI64 dtimestamp = d->QueryTimeStamp();
		fout << dtimestamp << "\n";
		PXCImage::ImageData depthData;
		d->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_DEPTH_F32, &depthData);
		memcpy(&depthFrames[i*width*height], depthData.planes[0], sizeof(float)*width*height);
		//Compute the UV map for this depth frame
		projection->QueryUVMap(d, &UVMaps[i*width*height]);
		d->ReleaseAccess(&depthData);
		sm->ReleaseFrame(); 

	}
	fout.close();

	//Step 2: Output frames
	//Allocate space for temporary buffers

	//Buffers for uv map
	BYTE* buf1_2w = new BYTE[width*height*4*2];
	BYTE* buf2_2w = new BYTE[width*height*4*2];

	//Buffers for point cloud
	BYTE* buf1_3w = new BYTE[width*height*4*3];
	BYTE* buf2_3w = new BYTE[width*height*4*3];

	//Buffer for color image
	BYTE* buf1_w = new BYTE[width*height*4];

	PXCPoint3DF32* points = new  PXCPoint3DF32[width*height];
	for (int i = 0; i < NFrames; i++) {
		//Step 1: Set up the point cloud in camera coordinates and do the projection
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				// populate the point with depth image pixel coordinates and the depth value
				points[y*width+x].x = (pxcF32)x;
				points[y*width+x].y = (pxcF32)y;
				points[y*width+x].z = depthFrames[i*width*height + y*width+x];
			}
		}

		projection->ProjectDepthToCamera(width*height, points, points);

		//Save point cloud in two parts
		for (int k = 0; k < width*height; k++) {
			BYTE* x = reinterpret_cast<BYTE*>(&points[k].x);
			BYTE* y = reinterpret_cast<BYTE*>(&points[k].y);
			BYTE* z = reinterpret_cast<BYTE*>(&points[k].z);
			for (int c = 0; c < 3; c++) {
				buf1_3w[k*4*3 + c] = x[c];
				buf1_3w[k*4*3 + 4 + c] = y[c];
				buf1_3w[k*4*3 + 8 + c] = z[c];
			}
			buf1_3w[k*4*3 + 3] = 0;
			buf1_3w[k*4*3 + 4 + 3] = 0;
			buf1_3w[k*4*3 + 8 + 3] = 0;
			for (int c = 0; c < 4; c++) {
				buf2_3w[k*4*3 + c] = x[3];
				buf2_3w[k*4*3 + 4 + c] = y[3];
				buf2_3w[k*4*3 + 8 + c] = z[3];
			}
		}
        WCHAR szScreenshotPath[MAX_PATH];
		StringCchPrintf(szScreenshotPath, _countof(szScreenshotPath), L"ADepth%i.bmp", i);
        HRESULT hr = SaveBitmapToFile(buf1_3w, width*3, height, 32, szScreenshotPath);
		StringCchPrintf(szScreenshotPath, _countof(szScreenshotPath), L"BDepth%i.bmp", i);
		SaveBitmapToFile(buf2_3w, width*3, height, 32, szScreenshotPath);

		//Step 2: Save the UV map in two parts
		for (int k = 0; k < width*height; k++) {
			BYTE* u = reinterpret_cast<BYTE*>(&UVMaps[i*width*height+k].x);
			BYTE* v = reinterpret_cast<BYTE*>(&UVMaps[i*width*height+k].y);
			for (int c = 0; c < 3; c++) {
				buf1_2w[k*4*2 + c] = u[c];
				buf1_2w[k*4*2 + 4 + c] = v[c];
			}
			buf1_2w[k*4*2 + 3] = 0;
			buf1_2w[k*4*2 + 4 + 3] = 0;
			for (int c = 0; c < 4; c++) {
				buf2_2w[k*4*2 + c] = u[3];
				buf2_2w[k*4*2 + 4 + c] = v[3];
			}
		}
		StringCchPrintf(szScreenshotPath, _countof(szScreenshotPath), L"AUV%i.bmp", i);
        SaveBitmapToFile(buf1_2w, width*2, height, 32, szScreenshotPath);
		StringCchPrintf(szScreenshotPath, _countof(szScreenshotPath), L"BUV%i.bmp", i);
		SaveBitmapToFile(buf2_2w, width*2, height, 32, szScreenshotPath);

		//Step 3: Convert color image from 3 bytes to 4 bytes and save
		for (int k = 0; k < width*height; k++) {
			for (int c = 0; c < 3; c++) {
				buf1_w[k*4+c] = colorFrames[i*width*height*3 + k*3 + c];
			}
			buf1_w[k*4+3] = 0;
		}
		StringCchPrintf(szScreenshotPath, _countof(szScreenshotPath), L"B-color%i.bmp", i);
		SaveBitmapToFile(buf1_w, width, height, 32, szScreenshotPath);
	}
	delete[] points;
	delete[] buf1_3w;
	delete[] buf2_3w;
	delete[] buf1_w;
	delete[] colorFrames;
	delete[] depthFrames;
	delete[] UVMaps;
	
	sm->Release();

	system("python convert.py"); //Convert to png using external python script
	system("del *.bmp"); //Cleanup bitmap files
}
