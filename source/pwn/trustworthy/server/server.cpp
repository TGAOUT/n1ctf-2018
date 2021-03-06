#include "stdafx.h"
#include <Windows.h>
#include <cstdio>
#include <TlHelp32.h>
#include <strsafe.h>

//#define DEBUG

#define BUFSIZE 512
#define PIPE_NAME TEXT("\\\\.\\pipe\\flag_server")
#define TOKEN_FILE TEXT("C:\\token.txt")

//char FLAG[] = "N1CTF{n0w_u_c4n_cla!m_windowz_proficiency!}";
char FLAG[] = "N1CTF{this_is_a_fake_flag}";
char FAIL[] = "No flag for you :p";

#ifdef DEBUG
#define dprintf printf
#else
#define dprintf(...)
#endif

void __declspec(noreturn) fatal(const char* str) {
	fprintf(stderr, "Error : %s %d\n", str, GetLastError());
	_exit(-1);
}

PVOID halloc(SIZE_T Size) {
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
}

void hfree(PVOID Ptr) {
	HeapFree(GetProcessHeap(), 0, Ptr);
}

PSECURITY_DESCRIPTOR GetFileSecurityDescriptor(const TCHAR* fileName) {
	PSECURITY_DESCRIPTOR pfSD = NULL;
	DWORD length;

	if (!GetFileSecurity(fileName, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, NULL, 0, &length)) {
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			pfSD = reinterpret_cast<PSECURITY_DESCRIPTOR>(halloc(length));
			if (!pfSD) {
				return NULL;
			}

			if (GetFileSecurity(fileName, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, pfSD, length, &length)) {
				return pfSD;
			}

			hfree(pfSD);
			pfSD = NULL;
		}
	}
	return pfSD;
}

HANDLE GetThreadToken(DWORD dwThreadId) {
	HANDLE hThread = NULL, hToken = NULL;;

	hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, dwThreadId);
	if (!hThread) {
		goto exit;
	}
	if (!OpenThreadToken(hThread, TOKEN_QUERY, FALSE, &hToken)) {
		hToken = NULL;
		goto exit;
	}
exit:
	if (hThread) {
		CloseHandle(hThread);
	}
	return hToken;
}

HANDLE GetProcessToken(DWORD dwProcessId) {
	HANDLE hProcess = NULL, hToken = NULL;

	hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwProcessId);
	if (!hProcess) {
		goto exit;
	}
	if (!OpenProcessToken(hProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &hToken)) {
		hToken = NULL;
		goto exit;
	}
exit:
	if (hProcess) {
		CloseHandle(hProcess);
	}
	return hToken;
}

BOOL CheckAccess(PSECURITY_DESCRIPTOR pfSD, HANDLE hToken) {
	GENERIC_MAPPING gm;
	DWORD granted;
	BOOL status;
	PRIVILEGE_SET priv;
	DWORD pslen = sizeof(priv);

	gm.GenericRead = FILE_GENERIC_READ;
	gm.GenericWrite = FILE_GENERIC_WRITE;
	gm.GenericExecute = FILE_GENERIC_EXECUTE;
	gm.GenericAll = FILE_ALL_ACCESS;
	ZeroMemory(&priv, sizeof(priv));
	priv.PrivilegeCount = 0;

	if (!AccessCheck(pfSD, hToken, FILE_READ_ACCESS, &gm, &priv, &pslen, &granted, &status)) {
		dprintf("AccessCheck failed %d\n", GetLastError());
		return FALSE;
	}
	if ((granted & FILE_READ_ACCESS) && (status == TRUE)) {
		return TRUE;
	}
	return FALSE;
}

BOOL DoAccessCheck(DWORD dwProcessId) {
	HANDLE hSnap;
	THREADENTRY32 th32;
	PSECURITY_DESCRIPTOR pfSD;
	BOOL r;
	HANDLE hToken, hProcessToken, hDupToken;
	BOOL result = FALSE;
	BOOL useprocess;

	pfSD = GetFileSecurityDescriptor(TOKEN_FILE);
	if (!pfSD) {
		dprintf("Cannot get security descriptor...\n");
		return FALSE;
	}

	hProcessToken = GetProcessToken(dwProcessId);
	if (!hProcessToken) {
		dprintf("Cannot get process token...\n");
		return FALSE;
	}

	if (!DuplicateToken(hProcessToken, SecurityImpersonation, &hDupToken)) {
		dprintf("Cannot get duplicate token...\n");
		return FALSE;
	}
	CloseHandle(hProcessToken);

	ZeroMemory(&th32, sizeof(th32));
	th32.dwSize = sizeof(th32);
	hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, -1);
	if (!hSnap) {
		dprintf("Cannot create snapshot...\n");
		return FALSE;
	}
	r = Thread32First(hSnap, &th32);
	while (r) {
		if (th32.th32OwnerProcessID == dwProcessId) {
			useprocess = FALSE;

			hToken = GetThreadToken(th32.th32ThreadID);
			if (!hToken) {
				useprocess = TRUE;
				hToken = hDupToken;
			}
			if (CheckAccess(pfSD, hToken)) {
				result = TRUE;
				break;
			}
			if (!useprocess) {
				CloseHandle(hToken);
			}
		}
		r = Thread32Next(hSnap, &th32);
	}
	CloseHandle(hSnap);
	CloseHandle(hDupToken);
	hfree(pfSD);
	
	return result;
}

DWORD WINAPI InstanceThread(LPVOID lpvParam) {
	HANDLE hPipe = (HANDLE)lpvParam;
	DWORD dwProcessId, written;

	if (!GetNamedPipeClientProcessId(hPipe, &dwProcessId)) {
		goto exit;
	}

	if (DoAccessCheck(dwProcessId)) {
		WriteFile(hPipe, FLAG, sizeof(FLAG), &written, NULL);
	}
	else {
		WriteFile(hPipe, FAIL, sizeof(FAIL), &written, NULL);
	}

exit:
	FlushFileBuffers(hPipe);
	DisconnectNamedPipe(hPipe);
	CloseHandle(hPipe);

	return 0;
}

int main(){
	for (;;)
	{
		HANDLE hPipe = CreateNamedPipe(
			PIPE_NAME,             // pipe name 
			PIPE_ACCESS_OUTBOUND,       // read/write access 
			PIPE_TYPE_BYTE |       // message type pipe 
			PIPE_READMODE_BYTE |   // message-read mode 
			PIPE_WAIT,                // blocking mode 
			PIPE_UNLIMITED_INSTANCES, // max. instances  
			BUFSIZE,                  // output buffer size 
			BUFSIZE,                  // input buffer size 
			0,                        // client time-out 
			NULL);                    // default security attribute 
		if (hPipe == INVALID_HANDLE_VALUE)
		{
			fatal("CreaateNamedPipe");
		}
		BOOL fConnected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
		if (fConnected)
		{
			DWORD dwThreadId;
			HANDLE hThread = CreateThread(
				NULL,              // no security attribute 
				0,                 // default stack size 
				InstanceThread,    // thread proc
				(LPVOID)hPipe,    // thread parameter 
				0,                 // not suspended 
				&dwThreadId);      // returns thread ID 

			if (hThread == NULL)
			{
				fatal("CreateThread");
			}
			else CloseHandle(hThread);
		}
		else {
			CloseHandle(hPipe);
		}
	}
    return 0;
}

