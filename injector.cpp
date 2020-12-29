#include <windows.h>
#include <TlHelp32.h>
#include <Shlwapi.h>
#include <stdio.h>
#include <winuser.h>

typedef bool _Bool;

bool injectDLL(DWORD processID);

#define PROCESSNAME "lmscope.exe"
#define PROCESSWINDOWNAME "Luminary Micro Oscilloscope"

//const char* dllName = "MyFilePath\\lmscopeHook.dll";  //Absolute file path is required!

int main() 
{
	PROCESSENTRY32 pe; 
	HANDLE snapshot;

	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if(snapshot != INVALID_HANDLE_VALUE) 
	{
		pe.dwSize = sizeof(PROCESSENTRY32);
		BOOL r = Process32First(snapshot, &pe);

		while(Process32Next(snapshot, &pe) && !StrStrIA(pe.szExeFile, PROCESSNAME)) 
		{
			pe.dwSize = sizeof(PROCESSENTRY32);   
		}

		DWORD processID = pe.th32ProcessID;  
		HWND windowHndl = FindWindow(NULL, PROCESSWINDOWNAME);

		if(windowHndl) 
		{
			GetWindowThreadProcessId(windowHndl, &processID);

			if(processID) 
			{
				if(injectDLL(processID))
				{
					puts("Injected");
				} 
				else 
				{
					puts("Unable to inject DLL");
				}
			} 
			else 
			{
				puts("Unable to find target process.");
			} 
		} 
		else 
		{
			puts("Unable to retrieve handle to target process");
		}
	} 
	else 
	{
		puts("Unable to retrieve process snapshot.");
	}

	return 0;
}


#define THREADACCESS (PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ)

#define stringify(s) _stringify(s)
#define _stringify(s) #s

bool injectDLL(DWORD processID) 
{
	if(processID) 
	{
		HANDLE process;
		LPVOID remoteString, loadLibraryAddress;

		process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processID);

		if(process) 
		{
			const char* dllName = stringify(DLL_NAME);
			loadLibraryAddress = (LPVOID)GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
			remoteString   = (LPVOID)VirtualAllocEx(process, NULL, strlen(dllName)+1, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

			if (WriteProcessMemory(process, (LPVOID)remoteString, dllName, strlen(dllName)+1, NULL)) 
			{ 
				HANDLE hndlThread = CreateRemoteThread(process, NULL, NULL, (LPTHREAD_START_ROUTINE)loadLibraryAddress, (LPVOID)remoteString, NULL, NULL);

				if(hndlThread) 
				{  
					WaitForSingleObject(hndlThread, INFINITE);

					VirtualFreeEx(process, remoteString, strlen(dllName)+1, MEM_DECOMMIT);
					CloseHandle(process);

					return 1;
				} 
				else 
				{
					puts("Unable to create remote thread");
				}   
			} 
			else 
			{
				puts("Write process memory access denied");
			}

			CloseHandle(process);

		} 
		else 
		{
			puts("Unable to open process");
		}
	} 
	else 
	{
		puts("Invalid process ID");
	}

	return 0;
}
