/*
	The MIT License (MIT)

	Copyright (c) 2013 Morten Erlandsen <animorten@gmail.com>

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <strsafe.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

int main(int argc, char** argv) {
	if(FAILED(CoInitialize(NULL))) {
		fprintf(stderr, "Initializing COM failed\n");
		return 1;
	}

	IMMDeviceEnumerator* enumerator;

	if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
		fprintf(stderr, "Creating device enumerator failed\n");
		return 1;
	}

	IMMDevice* device;

	if(FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
		fprintf(stderr, "Getting default audio endpoint failed\n");
		return 1;
	}

	IAudioClient* client;

	if(FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_SERVER, NULL, reinterpret_cast<void**>(&client)))) {
		fprintf(stderr, "Activating audio endpoint failed\n");
		return 1;
	}

	WAVEFORMATEX* format;

	if(FAILED(client->GetMixFormat(&format))) {
		fprintf(stderr, "Getting mixing format failed\n");
		return 1;
	}

	if(FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, format, NULL))) {
		fprintf(stderr, "Initializing audio client failed\n");
		return 1;
	}

	IAudioCaptureClient* capture;

	if(FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture)))) {
		fprintf(stderr, "Getting capture service failed\n");
		return 1;
	}

	TCHAR filename[MAX_PATH];
	TCHAR homepath[MAX_PATH];
	GetEnvironmentVariable(TEXT("HOMEPATH"), homepath, sizeof(homepath));
	StringCchPrintf(filename, sizeof(filename) / sizeof(TCHAR), TEXT("%s\\%u.wav"), homepath, time(NULL));

	HANDLE file = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);

	if(file == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Creating output file failed\n");
		return 1;
	}

	HANDLE mapping = CreateFileMapping(file, NULL, PAGE_READWRITE, 0, 268435456, NULL);

	if(mapping == NULL) {
		fprintf(stderr, "Creating file mapping failed\n");
		return 1;
	}

	BYTE* file_data = (BYTE*)MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 268435456);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	printf("Core Audio Mixer Format %u %u %u\n", format->nChannels, format->nSamplesPerSec, format->wBitsPerSample);

	if(FAILED(client->Start())) {
		fprintf(stderr, "Starting playback failed\n");
		return 1;
	}

	printf("Output file is \"%s\"\n", filename);
	printf("Recording, press right CTRL to stop recording\n");

	UINT32 position = 0;

	while(GetAsyncKeyState(VK_RCONTROL) >= 0) {
		BYTE* data;
		UINT32 size;
		DWORD flags;
		UINT64 device, performance;

		capture->GetNextPacketSize(&size);

		if(size > 0) {
			capture->GetBuffer(&data, &size, &flags, &device, &performance);

			if(flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
				printf("Buffer underrun detected\n");
			}

			for(UINT32 i = 0; i < size * format->nChannels; ++i, ++position) {
				reinterpret_cast<short*>(&file_data[44])[position] = static_cast<short>(reinterpret_cast<float*>(data)[i] * 32767.0f);
			}

			capture->ReleaseBuffer(size);
		}

		Sleep(1);
	}

	client->Stop();

	printf("Stopped recording\n");

	reinterpret_cast<UINT32*>(file_data)[0] = 0x46464952; // "RIFF"
	reinterpret_cast<UINT32*>(file_data)[1] = 44 + position * sizeof(short); // RIFF chunk size
	reinterpret_cast<UINT32*>(file_data)[2] = 0x45564157; // "WAVE"
	reinterpret_cast<UINT32*>(file_data)[3] = 0x20746d66; // "fmt "
	reinterpret_cast<UINT32*>(file_data)[4] = 16;
	reinterpret_cast<UINT16*>(file_data)[10] = WAVE_FORMAT_PCM;
	reinterpret_cast<UINT16*>(file_data)[11] = format->nChannels;
	reinterpret_cast<UINT32*>(file_data)[6] = format->nSamplesPerSec;
	reinterpret_cast<UINT32*>(file_data)[7] = format->nChannels * format->nSamplesPerSec * sizeof(short);
	reinterpret_cast<UINT16*>(file_data)[16] = format->nChannels * sizeof(short);
	reinterpret_cast<UINT16*>(file_data)[17] = sizeof(short) * 8;
	reinterpret_cast<UINT32*>(file_data)[9] = 0x61746164;
	reinterpret_cast<UINT32*>(file_data)[10] = position * sizeof(short);

	UnmapViewOfFile(file_data);
	CloseHandle(mapping);
	SetFilePointer(file, 44 + position * sizeof(short), 0, FILE_BEGIN);
	SetEndOfFile(file);
	CloseHandle(file);

	capture->Release();
	CoTaskMemFree(format);
	client->Release();
	device->Release();
	enumerator->Release();

	CoUninitialize();

	return 0;
}
