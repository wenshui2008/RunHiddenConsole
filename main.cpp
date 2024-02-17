//RunHiddenConsole.cpp
//copyright http://www.iavcast.com
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <io.h>
#include <locale.h>

HANDLE g_hStdin_Child_Rd = NULL;
HANDLE g_hStdin_Parent_Wr = NULL;
HANDLE g_hStdout_Parent_Rd = NULL;
HANDLE g_hStdout_Child_Wr = NULL;

#define MAX_FILEPATH 4096
TCHAR g_szMyPath[MAX_FILEPATH] = TEXT("");
size_t g_iMyPathLen = 0;

#define ErrorExit(s) do { printf(s); printf("\r\n"); return 0; } while(0)
#define CLOSEHANDLE(h) if(h) { CloseHandle(h); h = NULL; }
#define CloseAllPipes() do { CLOSEHANDLE(g_hStdin_Parent_Wr); CLOSEHANDLE(g_hStdout_Parent_Rd); } while(0)

#define MAX_COMMAND_LINE 65536
#define DEFAULT_CONSOLE_COLOR	( FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

static BOOL CreateStdioPipe()
{ 
	SECURITY_ATTRIBUTES saAttr; 

	// Set the bInheritHandle flag so pipe handles are inherited. 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 

	CloseAllPipes();

	// Create a pipe for reading the child process's STDOUT. 
	if (! CreatePipe(&g_hStdout_Parent_Rd, &g_hStdout_Child_Wr, &saAttr, 0)) 
		ErrorExit("Stdout pipe creation failed\n"); 

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if (! SetHandleInformation(g_hStdout_Parent_Rd, HANDLE_FLAG_INHERIT, 0) )
		ErrorExit("Stdout SetHandleInformation");

	// Create a pipe for writing to the child process's STDIN.
	if (! CreatePipe(&g_hStdin_Child_Rd, &g_hStdin_Parent_Wr, &saAttr, 0)) 
		ErrorExit("Stdin CreatePipe"); 

	// Ensure the write handle to the pipe for STDIN is not inherited. 
	if ( ! SetHandleInformation(g_hStdin_Parent_Wr, HANDLE_FLAG_INHERIT, 0) )
		ErrorExit("Stdin SetHandleInformation"); 

	return 1;
}

//
// 
// Remove ..\ from path 
// ie: c:\abc\bcd\cde\efg\..\..\..\fgh\ghi.ext => c:\abc\fgh\ghi.ext
// nPathLength - the length of return string
// the return string is allocated by malloc

static LPTSTR FixRelativePath(LPCTSTR pszPath, int &nOutputLength)
{
	LPCTSTR pszPathEnd,pszNameBegin,pszNameEnd;
	LPTSTR pszReturn = NULL;

	int nPathLength;
	int nFileNameLength ;

	pszPathEnd = _tcsstr(pszPath,_TEXT("..\\"));

	if (!pszPathEnd) {
		pszReturn = _tcsdup(pszPath);
		nOutputLength = (int)_tcslen(pszReturn);
		return pszReturn;
	}

	pszNameBegin = pszPathEnd;
	pszPathEnd --;

	nPathLength = (int) (pszPathEnd - pszPath + 1);

	nFileNameLength = (int)_tcslen(pszNameBegin);
	pszNameEnd = pszNameBegin + nFileNameLength;

	while(pszNameBegin < pszNameEnd) {
		//check "..\"
		if (*pszNameBegin != _T('.') || 
			*(pszNameBegin + 1) != _T('.') || 
			*(pszNameBegin + 2) != _T('\\'))
			break;
		pszNameBegin += 3;

		pszPathEnd --;
		while(pszPathEnd > pszPath) {
			if (*pszPathEnd == _T('\\'))
				break;
			pszPathEnd --;
		}
	}

	if (pszPathEnd > pszPath && pszNameBegin <= pszNameEnd) {
		nPathLength = int(pszPathEnd - pszPath + 1);
		nFileNameLength = int(pszNameEnd - pszNameBegin + 1);
		pszReturn = (TCHAR*) malloc( sizeof (TCHAR) * (nPathLength + 1 + nFileNameLength + 1));
		if (!pszReturn) return NULL;
		memcpy(pszReturn,pszPath, sizeof (TCHAR) * nPathLength);
		if (pszReturn[nPathLength - 1] != _T('\\')) {
			pszReturn[nPathLength] = _T('\\');
			nPathLength ++;
		}
		memcpy(pszReturn + nPathLength,pszNameBegin, sizeof (TCHAR) * nFileNameLength);
		nOutputLength = nPathLength + nFileNameLength;
		pszReturn[nOutputLength] = _T('\0');
	}

	return pszReturn;
}

static HANDLE process_job_handle = NULL;

int  CreateChildProcessJob()
{
	process_job_handle = CreateJobObject(NULL, NULL);

	if (process_job_handle) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info;
		BOOL set_auto_kill_ok;

		memset(&limit_info, 0x0, sizeof(limit_info));
		limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		set_auto_kill_ok = SetInformationJobObject(
			process_job_handle, 
			JobObjectExtendedLimitInformation, 
			&limit_info, 
			sizeof(limit_info)
			);
		if (!set_auto_kill_ok) {
			CloseHandle(process_job_handle);
			process_job_handle = NULL;

			return -1;
		}
	}

	return 0;
}

static
void DestroyChildProcessJob()
{
	if (process_job_handle) {
		CloseHandle(process_job_handle);
		process_job_handle = NULL;
	}
}

//
//
// return 0: no error
// otherwise: error code
//

static
BOOL CreateChildProcess(HANDLE *pChildHandle, DWORD *pid, BOOL bPrintLog, LPTSTR pszCommandLine, LPCTSTR pszOutputFile, LPCTSTR pszCurrentDirectory)
{
	BOOL bReturn;
	STARTUPINFO si; 
	PROCESS_INFORMATION pi;
	LPTSTR pszEvnVar;

	if (!CreateStdioPipe()) {
		return FALSE;
	}

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si); 
	ZeroMemory(&pi, sizeof(pi));

	pszEvnVar = (LPTSTR)GetEnvironmentStrings();

	HANDLE hFileStdOut = g_hStdout_Child_Wr;

	if (pszOutputFile) {
		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		sa.lpSecurityDescriptor = NULL;

		hFileStdOut = CreateFile(pszOutputFile, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, &sa, CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFileStdOut == INVALID_HANDLE_VALUE) {
			_tprintf(TEXT("Create output file %s failed\n"),pszOutputFile);
			return FALSE;
		}
	}

	si.cb = sizeof(STARTUPINFO); 
	si.hStdError = hFileStdOut;
	si.hStdOutput = hFileStdOut;
	si.hStdInput = g_hStdin_Child_Rd;
	si.dwFlags = STARTF_USESTDHANDLES;

	if (bPrintLog) {
		_tprintf(TEXT("Starting %s"),pszCommandLine);
	}

	bReturn = CreateProcess(NULL, pszCommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, pszEvnVar, pszCurrentDirectory, &si, &pi);

	FreeEnvironmentStrings(pszEvnVar);

	// Close handles to the stdin and stdout pipes no longer needed by the child process.
	CloseHandle(g_hStdout_Child_Wr);
	CloseHandle(g_hStdin_Child_Rd);

	CloseHandle(pi.hThread);

	if (hFileStdOut != g_hStdout_Child_Wr)
		CloseHandle(hFileStdOut);

	if (process_job_handle) {
		BOOL b = AssignProcessToJobObject(process_job_handle, pi.hProcess);
		if (!b) {
			_tprintf(TEXT(" AssignProcessToJobObject Failed!"));
		}
	}

	*pChildHandle = pi.hProcess;
	*pid = pi.dwProcessId;

	return TRUE;
}

static
void SavePidToFile(LPCTSTR pszPidFile, DWORD dwPid)
{
	if (pszPidFile) {
		HANDLE hFilePid = CreateFile(pszPidFile, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFilePid != INVALID_HANDLE_VALUE) {
			char szBuff[64];
			//DWORD dwPid = GetProcessId(pi.hProcess);
			DWORD dwWrite = 0;
			int len = sprintf_s(szBuff, ARRAYSIZE(szBuff), "%u", dwPid);
			WriteFile(hFilePid, szBuff, len, &dwWrite, NULL);

			CloseHandle(hFilePid);
		}
	}
}

static 
BOOL Fork()
{
	STARTUPINFO si; 
	PROCESS_INFORMATION pi;
	LPTSTR pszEvnVar;
	BOOL bReturn;
	HANDLE hStdOutRead, hStdoutWrite;
	SECURITY_ATTRIBUTES saAttr; 
	LPTSTR pszCommandLine = GetCommandLine();
	LPTSTR pszPos = _tcsstr(pszCommandLine, TEXT("/r"));

	if (!pszPos) _tcsstr(pszCommandLine, TEXT("-r"));

	if (!pszPos)
		return FALSE;

	pszPos[1] = 'R';

	// Set the bInheritHandle flag so pipe handles are inherited. 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 
	
	if (CreatePipe(&hStdOutRead, &hStdoutWrite, &saAttr, 256)) {
		SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
	}

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si); 
	ZeroMemory(&pi, sizeof(pi));

	si.hStdError = hStdoutWrite;
	si.hStdOutput = hStdoutWrite;
	si.hStdInput = NULL;
	si.dwFlags = STARTF_USESTDHANDLES;

	pszEvnVar = (LPTSTR)GetEnvironmentStrings();

	bReturn = CreateProcess(NULL, pszCommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, pszEvnVar, NULL, &si, &pi);

	if (bReturn) {
#define BUFSIZE 4096
		BYTE chBuf[BUFSIZE];
		BOOL bSuccess;
		DWORD dwRead = 0, dwAvail = 0;
		DWORD dwWait = WaitForSingleObject(pi.hProcess, 200);

		if (dwWait == 0)
			bReturn = FALSE;

		if (PeekNamedPipe(hStdOutRead, NULL, NULL, &dwRead, &dwAvail, NULL)) {
			if ( dwAvail > 0) {
				if (dwAvail > BUFSIZE)
					dwAvail = BUFSIZE;

				bSuccess = ReadFile( hStdOutRead, chBuf, dwAvail, &dwRead, NULL);
				if (bSuccess) {
					DWORD dwWritten;
					HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
					WriteFile(hStdOut, chBuf, dwRead, &dwWritten, NULL);
				}
			}
		}
	}

	FreeEnvironmentStrings(pszEvnVar);

	CloseHandle(hStdoutWrite);
	CloseHandle(hStdOutRead);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	return bReturn;
}

void Usage() {
	printf("RunHiddenConsole Usage:\n"
		"RunHiddenConsole.exe [/l] [/w] [/r] [/n name] [/k name] [/o output-file] [/p pidfile] commandline\n"
	 "For example:\n"
	 "RunHiddenConsole.exe /l /r e:\\WNMP\\PHP\\php-cgi.exe -b 127.0.0.1:9000 -c e:\\WNMP\\php\\php.ini\n"
	 "RunHiddenConsole.exe /l /r E:/WNMP/nginx/nginx.exe -p E:/WNMP/nginx\n"
	 "The /l is optional, printing the result of process startup\n"
	 "The /w is optional, waiting for termination of the process\n"
	 "The /o is optional, redirecting the output of the program to a file\n"
	 "The /p is optional, saving the process id to a file\n"
	 "The /r is optional, supervise the child process, if the child process exits, restart the child process\n"
	 "The /n is optional, naming control signals\n"
	 "The /k is optional, kill the daemon according to the specified control signal\n");
}

int _tmain(int nArgc, _TCHAR ** ppArgv)
{
	TCHAR * pch;
	TCHAR * pszExePath, szExePath[MAX_FILEPATH], szCurrentDirectory[MAX_FILEPATH];
	TCHAR * pszCommandLine = NULL, *pszOutputFile = NULL, *pszPidFile = NULL, *pszSignalName = NULL;
	BOOL bHasSpace;
	BOOL bReturn;
	BOOL bWaitExit = 0, bPrintLog = 0, bResume = 0, bFork = 0, bKill = 0;
	int  iCmdLinePos = 1, i;
	HANDLE hStdOut;
	HANDLE hChildProcess = NULL;
	DWORD dwChildPid = 0;
	HANDLE hEventExit = NULL;

	GetModuleFileName(NULL,g_szMyPath, ARRAYSIZE(g_szMyPath));

	if (_tcsstr(g_szMyPath,TEXT("..\\"))) {
		int nTextLength = (int)_tcslen(g_szMyPath);
		LPTSTR pszDir = FixRelativePath(g_szMyPath, nTextLength);

		if (pszDir) {
			_tcscpy(g_szMyPath, pszDir);

			free(pszDir);
		}
	}

	pch = _tcsrchr(g_szMyPath,'\\');
	if (pch) {
		pch ++;
		*pch = 0;
	}
	else {
		//Never arrived here!
		return -2;
	}
	
	g_iMyPathLen = pch - g_szMyPath;

	if (nArgc < 2) {
		Usage();
		//
		return -1;
	}

	for (i=1;i <nArgc;i++){
		if (  (ppArgv[i][0] == '-') || (ppArgv[i][0] == '/') )	{
			if (_tcslen(ppArgv[i]) == 2) {
				switch (tolower(ppArgv[i][1])){
				case 'l':
					bPrintLog = 1;
					break;

				case 'w':
					bWaitExit = 1;
					break;
				case 'o':
					if (i <nArgc -1) {
						i++;
						iCmdLinePos ++;
						pszOutputFile = ppArgv[i];
					}
					else {
						_tprintf(TEXT("No output file!\n"));
						Usage();
						return -1;
					}
					
					break;
				case 'p':
					if (i <nArgc -1) {
						i++;
						iCmdLinePos ++;
						pszPidFile = ppArgv[i];
					}
					else {
						_tprintf(TEXT("No process id file!\n"));
						Usage();
						return -1;
					}

					break;
				case 'n':
					if (i <nArgc -1) {
						i++;
						iCmdLinePos ++;
						pszSignalName = ppArgv[i];

						if (_tcslen(pszSignalName) > 32) {
							_tprintf(TEXT("The signal name is too long, it should be less than 32 characters!\n"));
							Usage();
							return -1;
						}
					}
					else {
						_tprintf(TEXT("No signal name specified!\n"));
						Usage();
						return -1;
					}
					break;
				case 'r':
					bFork = 1;  // fork a child process as deamon
					break;
				case 'R':
					bResume = 1;
					break;
				case 'k':
					bKill = 1;
					break;
				}
			}
			
			iCmdLinePos ++;
		}
		else {
			break;
		}
	}
	
	if (bKill) {
		TCHAR szSignalName[64];

		if (!pszSignalName) {
			printf("Kill without signal name.\n");

			return -1;
		}

		_stprintf_s(szSignalName, ARRAYSIZE(szSignalName), TEXT("Global\\rhc_exit_%s"), pszSignalName);

		hEventExit = OpenEvent(EVENT_MODIFY_STATE, FALSE, szSignalName);

		if (hEventExit) {
			SetEvent(hEventExit);
		}

		return 0;
	}

	if (iCmdLinePos >= nArgc) {
		return -1;
	}

	if (bResume && pszSignalName) {
		TCHAR szSignalName[64];

		_stprintf_s(szSignalName, ARRAYSIZE(szSignalName), TEXT("Global\\rhc_exit_%s"), pszSignalName);

		hEventExit = OpenEvent(0, FALSE, szSignalName);

		if (hEventExit) {
			_tprintf(TEXT("Process with signal name %s is already existed.\n"), pszSignalName);

			CloseHandle(hEventExit);

			return 0;
		}
	}

	if (bFork) {
		if (Fork())
			return 0;

		bResume = 1;
	}

	hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

	if (GetACP() == 936) {
		setlocale(LC_ALL, "chs");
	}
	
	pszExePath = ppArgv[iCmdLinePos];
	if (pszExePath[1] != ':') {
		_tcscpy(szExePath,g_szMyPath);
		_tcscpy(&szExePath[g_iMyPathLen],pszExePath);
	}
	else {
		_tcscpy(szExePath,pszExePath);
	}
	pszExePath = szExePath;

	pch = pszExePath;
	while(*pch) {
		if (*pch == '/') {
			*pch = '\\';
		}

		pch ++;
	}

	_tcscpy(szCurrentDirectory,pszExePath);
	pch = _tcsrchr(szCurrentDirectory,'\\');
	*pch = 0;

	pszCommandLine = (TCHAR *)malloc(sizeof (TCHAR) * MAX_COMMAND_LINE);
	if (!pszCommandLine) {
		return -3;
	}

	pch = pszCommandLine;

	bHasSpace = _tcschr(pszExePath, ' ') != NULL;

	if (bHasSpace) *pch++ = '\"';
	_tcscpy(pch,pszExePath);
	pch += _tcslen(pszExePath);
	if (bHasSpace) *pch++ = '\"';
	
	for (i = iCmdLinePos + 1; i < nArgc; i ++) {
		_TCHAR * argv = ppArgv[i];

		*pch++ = ' ';

		bHasSpace = _tcschr(argv, ' ') != NULL;

		if (bHasSpace) *pch++ = '\"';
		_tcscpy(pch, argv);
		pch += _tcslen(argv);
		if (bHasSpace) *pch++ = '\"';
	}
	
	*pch = 0;
	
	if (bResume) {
		CreateChildProcessJob();
	}
	
	bReturn = CreateChildProcess(&hChildProcess, &dwChildPid, bPrintLog, pszCommandLine, pszOutputFile, szCurrentDirectory);

	if (!bReturn) {
		DWORD dwError = GetLastError();

		if (bPrintLog) {
			SetConsoleTextAttribute(hStdOut, FOREGROUND_RED ); 
			_tprintf(TEXT(" Failed!"), dwError);
			SetConsoleTextAttribute(hStdOut, DEFAULT_CONSOLE_COLOR );

			_tprintf(TEXT(",Error Code:%u\n"), dwError);
		}

		return -5;
	}

	if (bPrintLog) {
		SetConsoleTextAttribute(hStdOut, FOREGROUND_GREEN ); 
		_tprintf(TEXT(" Success!\n"));
		SetConsoleTextAttribute(hStdOut, DEFAULT_CONSOLE_COLOR); 
	}
	if (bPrintLog) {
		fflush(stdout);
	}

	SavePidToFile(pszPidFile, dwChildPid);

	if (bWaitExit || bResume) {
		
		if (pszSignalName) {
			TCHAR szSignalName[64];

			_stprintf_s(szSignalName, ARRAYSIZE(szSignalName), TEXT("Global\\rhc_exit_%s"), pszSignalName);

			hEventExit = CreateEvent(NULL, TRUE, FALSE, szSignalName);
		}
		// Supervise the child process, if the child process exits, restart the child process
		while(TRUE) {
			HANDLE handles[2];
			DWORD dwWait, nCount = 1;
						
			handles[0] = hChildProcess;
			if (hEventExit) {
				handles[1] = hEventExit;
				nCount++;
			}
			
			dwWait = WaitForMultipleObjects(nCount, handles, FALSE, INFINITE);

			if (dwWait == WAIT_OBJECT_0) {
				CloseHandle(hChildProcess);

				if (!bResume)
					break;

				bReturn = CreateChildProcess(&hChildProcess, &dwChildPid, FALSE, pszCommandLine, pszOutputFile, szCurrentDirectory);

				if (bReturn)
					SavePidToFile(pszPidFile, dwChildPid);
				else
					break;
			}
			else if (dwWait == WAIT_OBJECT_0 + 1) {
				// Received exit signal
				break;
			}
			else {
				// System error
				break;
			}
		}
	}

	free(pszCommandLine);

	if (hEventExit) {
		CloseHandle(hEventExit);
	}

	DestroyChildProcessJob();
			
	return 0;
}


