#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <Winusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <complex>

typedef BOOL(WINAPI *pWinUSB_ReadPipe)(
	WINUSB_INTERFACE_HANDLE,
	UCHAR,
	PUCHAR,
	ULONG,
	PULONG,
	LPOVERLAPPED
);


BOOL WINAPI dWinUSB_ReadPipe(
	WINUSB_INTERFACE_HANDLE,
	UCHAR,
	PUCHAR,
	ULONG,
	PULONG,
	LPOVERLAPPED
);


#define OVERWRITELENGTH 6
#define PROCESSWINDOWNAME "Luminary Micro Oscilloscope"

#define PACKETHEADERSIZE  sizeof(ScopePacket)
#define DATAPACKETHEADERSIZE sizeof(ScopeDataStart)
#define PACKETDATASIZE   sizeof(ScopeDataElement)
#define SAMPLECOUNT 8192

#define SCOPE_PROTOCOL_VERSION 0x01
#define SCOPE_PKT_DATA_START 0x86
#define SCOPE_PKT_DATA 0x87
#define SCOPE_PKT_DATA_END 0x88

#pragma pack(push, 1)
typedef struct ScopePacket
{
	unsigned char version;
	unsigned char hdrLength;
	unsigned char packetType;
	unsigned char cParam;
	unsigned long lParam;
	unsigned long dataLength;
};
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct ScopeDataStart
{
	unsigned long sampleOffsetuS;
	unsigned long samplePerioduS;
	unsigned long triggerIndex;
	unsigned long totalElements;
	unsigned char dualChannel;
	unsigned char ch2SampleFirst;
};
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct ScopeDataElement
{
	unsigned long timeuS;
	short samplemVolts;
};
#pragma pack(pop)


pWinUSB_ReadPipe pWinUsbReadPipeOriginal = 0;

BYTE oldCodeReadPipe[OVERWRITELENGTH] = { 0 };
BYTE jmpReadPipe[OVERWRITELENGTH] = { 0 };

static DWORD oldProtect;
static DWORD newProtect = PAGE_EXECUTE_READWRITE;

static HANDLE hThread;
static HWND targetWindowHndl;

static ScopePacket packetHeader;
static ScopeDataStart dataPacketHeader;

static float sampleBuffers[2][SAMPLECOUNT + 1];
static float* workingBuffer;

static int bufferUsed;
static int currentSampleCount;
static float samplingRateuHz;

void redirect(LPVOID, LPVOID, BYTE*, BYTE*);
DWORD WINAPI calculateFrequency(LPVOID param);

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) 
{
	 switch (fdwReason) 
	 {
	 	case DLL_PROCESS_ATTACH: 
		{
	  		HMODULE moduleHndlWUSB = GetModuleHandleA("winusb.dll");
	      HMODULE moduleHndlK32 = GetModuleHandleA("kernel32.dll");
	
	  		pWinUsbReadPipeOriginal = (pWinUSB_ReadPipe)GetProcAddress(moduleHndlWUSB, "WinUsb_ReadPipe");
	
	  		if (pWinUsbReadPipeOriginal != NULL) 
	  		{
				targetWindowHndl = FindWindowA(0, PROCESSWINDOWNAME);
	
				if (targetWindowHndl) 
				{
					redirect(dWinUSB_ReadPipe, pWinUsbReadPipeOriginal, oldCodeReadPipe, jmpReadPipe);
	
					DWORD threadID;
	
					hThread = CreateThread(0, 0, calculateFrequency, 0, CREATE_SUSPENDED, &threadID);
				}
				else 
				{
					MessageBoxA(0, "Failed to find the target window.", 0, MB_OK);
				}
	  		}
	 	}break;
	
		case DLL_PROCESS_DETACH:
	  		memcpy(pWinUsbReadPipeOriginal, oldCodeReadPipe, OVERWRITELENGTH);
	 	case DLL_THREAD_ATTACH:
	 	case DLL_THREAD_DETACH:
	  break;
	 }

 return TRUE;
}

void redirect(LPVOID newFunc, LPVOID origFunc, BYTE* oldCode, BYTE* jmp) 
{
	BYTE tempJmp[OVERWRITELENGTH] = 
	{
		0xE9, 0x90, 0x90,
		0x90, 0x90, 0xC3
	};

	memcpy(jmp, tempJmp, OVERWRITELENGTH);

	DWORD jmpSize = ((DWORD)newFunc - (DWORD)origFunc - 5);

	VirtualProtect((LPVOID)origFunc, OVERWRITELENGTH, newProtect, &oldProtect);

	memcpy(oldCode, origFunc, OVERWRITELENGTH);
	memcpy(&jmp[1], &jmpSize, 4);
	memcpy(origFunc, jmp, OVERWRITELENGTH);

	DWORD tempOldProtect;

	VirtualProtect((LPVOID)origFunc, OVERWRITELENGTH, oldProtect, &tempOldProtect);
}

BOOL WINAPI dWinUSB_ReadPipe(WINUSB_INTERFACE_HANDLE interfaceHndl,
 									  UCHAR pipeID, PUCHAR buffer, ULONG bufferLength,
 									  PULONG lengthTransferred, LPOVERLAPPED overlapped) 
{
	 DWORD temp;
	 VirtualProtect((LPVOID)pWinUsbReadPipeOriginal, OVERWRITELENGTH, newProtect, &temp);
	
	 memcpy(pWinUsbReadPipeOriginal, oldCodeReadPipe, OVERWRITELENGTH);
	
	 BOOL originalReturn = pWinUsbReadPipeOriginal(interfaceHndl, pipeID, buffer,
	  bufferLength, lengthTransferred,
	  overlapped);
	
	 memcpy(pWinUsbReadPipeOriginal, jmpReadPipe, OVERWRITELENGTH);
	 VirtualProtect((LPVOID)pWinUsbReadPipeOriginal, OVERWRITELENGTH, oldProtect, &temp);
	
	 DWORD numOfBytesTransferred = bufferLength;



 // ---- For single- channel only ----

 	switch (packetHeader.packetType) 
 	{

		case SCOPE_PKT_DATA_START: 
		{	
				ScopeDataElement tempElement;
				memcpy(&tempElement, buffer + PACKETDATASIZE, PACKETDATASIZE);
				samplingRateuHz = 1.0f / (float)tempElement.timeuS;

				packetHeader.packetType = 0;
		}break;
		
		case SCOPE_PKT_DATA: 
		{
			int length = packetHeader.dataLength / PACKETDATASIZE;
			int newCount = currentSampleCount + length;

			int c = 0;

			if (newCount <= SAMPLECOUNT) 
			{
				c = length;
			}
			else if (currentSampleCount < SAMPLECOUNT) 
			{
				c = SAMPLECOUNT - currentSampleCount;
			}
			else 
			{

				if (ResumeThread(hThread) != 0) 
				{
					workingBuffer = sampleBuffers[bufferUsed];
					bufferUsed ^= 0x1;
				}

				currentSampleCount = 0;
			}

			ScopeDataElement* elementBuffer = (ScopeDataElement*)buffer;

			for (int i = 0; i < c; i++) 
			{
				sampleBuffers[bufferUsed][currentSampleCount + i] = elementBuffer[i].samplemVolts;
			}

			currentSampleCount += c;

			packetHeader.packetType = 0;
		 }break;
		
		 case SCOPE_PKT_DATA_END: 
		 {
			packetHeader.packetType = 0;
		 }break;
		
		 default: 
		 {
		  if (buffer[0] == SCOPE_PROTOCOL_VERSION) 
		  {
			  memcpy(&packetHeader, buffer, PACKETHEADERSIZE);
		  }
		 }break;
		
	}

	return originalReturn;
}


void rfft(float X[], int N);

DWORD WINAPI calculateFrequency(LPVOID param)
{
	for (;;) 
	{

		rfft(workingBuffer, SAMPLECOUNT);

		int maxI = 1;
		float maxVal = 0;

		for (int i = 2; i < SAMPLECOUNT / 2 + 1; ++i) {

			float real = workingBuffer[i];
			float imag = workingBuffer[SAMPLECOUNT - i + 2];

			float temp = real * real + imag * imag;

			if (temp > maxVal) {
				maxVal = temp;
				maxI = i;
			}
		}


		float frequency = maxI * samplingRateuHz * 1000000 / SAMPLECOUNT;

		char tBuf[20];
		sprintf(tBuf, "f: %0.2f Hz", frequency);

		SetWindowTextA(targetWindowHndl, tBuf);

		SuspendThread(GetCurrentThread());

	}

}


/****************************************************************************
* rfft(float X[],int N)                                                     *
*     A real-valued, in-place, split-radix FFT program                      *
*     Decimation-in-time, cos/sin in second loop                            *
*     Input: float X[1]...X[N] (NB Fortran style: 1st pt X[1] not X[0]!)    *
*     Length is N=2**M (i.e. N must be power of 2--no error checking)       *
*     Output in X[1]...X[N], in order:                                      *
*           [Re(0), Re(1),..., Re(N/2), Im(N/2-1),..., Im(1)]               *
*                                                                           *
* Original Fortran code by Sorensen; published in H.V. Sorensen, D.L. Jones,*
* M.T. Heideman, C.S. Burrus (1987) Real-valued fast fourier transform      *
* algorithms.  IEEE Trans on Acoustics, Speech, & Signal Processing, 35,    *
* 849-863.  Adapted to C by Bill Simpson, 1995  wsimpson@uwinnipeg.ca       *
****************************************************************************/

void rfft(float X[], int N)
{
 int I, I0, I1, I2, I3, I4, I5, I6, I7, I8, IS, ID;
 int J, K, M, N2, N4, N8;
 float A, A3, CC1, SS1, CC3, SS3, E, R1, XT;
 float T1, T2, T3, T4, T5, T6;

 M = (int)(log(N) / log(2.0));               /* N=2^M */

 /* ----Digit reverse counter--------------------------------------------- */
 J = 1;
 for (I = 1; I<N; I++)
 {
  if (I<J)
  {
   XT = X[J];
   X[J] = X[I];
   X[I] = XT;
  }
  K = N / 2;
  while (K<J)
  {
   J -= K;
   K /= 2;
  }
  J += K;
 }

 /* ----Length two butterflies--------------------------------------------- */
 IS = 1;
 ID = 4;
 do
 {
  for (I0 = IS; I0 <= N; I0 += ID)
  {
   I1 = I0 + 1;
   R1 = X[I0];
   X[I0] = R1 + X[I1];
   X[I1] = R1 - X[I1];
  }
  IS = 2 * ID - 1;
  ID = 4 * ID;
 } while (IS<N);
 /* ----L shaped butterflies----------------------------------------------- */
 N2 = 2;
 for (K = 2; K <= M; K++)
 {
  N2 = N2 * 2;
  N4 = N2 / 4;
  N8 = N2 / 8;
  E = (float) 6.2831853071719586f / N2;
  IS = 0;
  ID = N2 * 2;
  do
  {
   for (I = IS; I<N; I += ID)
   {
    I1 = I + 1;
    I2 = I1 + N4;
    I3 = I2 + N4;
    I4 = I3 + N4;
    T1 = X[I4] + X[I3];
    X[I4] = X[I4] - X[I3];
    X[I3] = X[I1] - T1;
    X[I1] = X[I1] + T1;
    if (N4 != 1)
    {
     I1 += N8;
     I2 += N8;
     I3 += N8;
     I4 += N8;
     T1 = (X[I3] + X[I4])*.7071067811865475244f;
     T2 = (X[I3] - X[I4])*.7071067811865475244f;
     X[I4] = X[I2] - T1;
     X[I3] = -X[I2] - T1;
     X[I2] = X[I1] - T2;
     X[I1] = X[I1] + T2;
    }
   }
   IS = 2 * ID - N2;
   ID = 4 * ID;
  } while (IS<N);
  A = E;
  for (J = 2; J <= N8; J++)
  {
   A3 = 3.0f * A;
   CC1 = cos(A);
   SS1 = sin(A);  /*typo A3--really A?*/
   CC3 = cos(A3); /*typo 3--really A3?*/
   SS3 = sin(A3);
   A = (float)J * E;
   IS = 0;
   ID = 2 * N2;
   do
   {
    for (I = IS; I<N; I += ID)
    {
     I1 = I + J;
     I2 = I1 + N4;
     I3 = I2 + N4;
     I4 = I3 + N4;
     I5 = I + N4 - J + 2;
     I6 = I5 + N4;
     I7 = I6 + N4;
     I8 = I7 + N4;
     T1 = X[I3] * CC1 + X[I7] * SS1;
     T2 = X[I7] * CC1 - X[I3] * SS1;
     T3 = X[I4] * CC3 + X[I8] * SS3;
     T4 = X[I8] * CC3 - X[I4] * SS3;
     T5 = T1 + T3;
     T6 = T2 + T4;
     T3 = T1 - T3;
     T4 = T2 - T4;
     T2 = X[I6] + T6;
     X[I3] = T6 - X[I6];
     X[I8] = T2;
     T2 = X[I2] - T3;
     X[I7] = -X[I2] - T3;
     X[I4] = T2;
     T1 = X[I1] + T5;
     X[I6] = X[I1] - T5;
     X[I1] = T1;
     T1 = X[I5] + T4;
     X[I5] = X[I5] - T4;
     X[I2] = T1;
    }
    IS = 2 * ID - N2;
    ID = 4 * ID;
   } while (IS<N);
  }
 }
 return;
}

