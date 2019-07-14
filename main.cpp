//RunHiddenConsole.cpp
//copyright http://www.iavcast.com
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <io.h>
#include <locale.h>

HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;

#define MAX_FILEPATH 4096
TCHAR g_szMyPath[MAX_FILEPATH] = TEXT("");
size_t g_iMyPathLen = 0;

#define ErrorExit(s) do { printf(s);printf("\r\n"); return 0; } while(0)

BOOL InitStdOut() 
{ 
	SECURITY_ATTRIBUTES saAttr; 

	// Set the bInheritHandle flag so pipe handles are inherited. 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 

	// Create a pipe for the child process's STDOUT. 
	if (! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0)) 
		ErrorExit("Stdout pipe creation failed\n"); 

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) )
		ErrorExit("Stdout SetHandleInformation");

	if (! CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0)) 
		ErrorExit("Stdin CreatePipe"); 

	// Ensure the write handle to the pipe for STDIN is not inherited. 
	if ( ! SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0) )
		ErrorExit("Stdin SetHandleInformation"); 

	return 1;
}

//
// cat
// Remove ..\ from path 
// ie: c:\abc\bcd\cde\efg\..\..\..\fgh\ghi.ext = c:\abc\fgh\ghi.ext
// nTextLength - the length of return string
// the return string is allocated by malloc

LPTSTR FixPathFileNameString(LPCTSTR pszPath,int &nTextLength)
{
	LPCTSTR pszPathEnd,pszNameBegin,pszNameEnd;
	LPTSTR pszReturn = NULL;

	int nPathLength;
	int nFileNameLength ;

	pszPathEnd = _tcsstr(pszPath,_TEXT("..\\"));

	if (!pszPathEnd) {
		pszReturn = _tcsdup(pszPath);
		nTextLength = (int)_tcslen(pszReturn);
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
		nTextLength = nPathLength + nFileNameLength;
		pszReturn[nTextLength] = _T('\0');
	}

	return pszReturn;
}

#define MAX_COMMAND_LINE 65536

void Usage() {
	printf("RunHiddenConsole Usage:\n"
		"RunHiddenConsole.exe [/l] [/w] [/o output-file] commandline\n"
	 "For example:\n"
	 "RunHiddenConsole.exe /l e:\\WNMP\\PHP\\php-cgi.exe -b 127.0.0.1:9000 -c e:\\WNMP\\php\\php.ini\n"
	 "RunHiddenConsole.exe /l E:/WNMP/nginx/nginx.exe -p E:/WNMP/nginx\n"
	 "The /l is optional, which means printing the result of process startup\n"
	 "The /w is optional, which means waiting for termination of the process\n"
	 "The /o is optional, which means redirectingthe output of the program to a file\n");
}

int _tmain(int _Argc, _TCHAR ** _Argv)
{
	TCHAR * pch;
	TCHAR * pszExePath,szExePath[MAX_FILEPATH];
	TCHAR szCurrentDirectory[MAX_FILEPATH];
	TCHAR * pszCommandLine = NULL,*pszOutputFile = NULL;
	BOOL bHasSpace;
	BOOL bReturn;
	LPTSTR pszEvnVar;
	BOOL bWaitExit = 0,bPrintLog = 0;
	STARTUPINFO si; 
	PROCESS_INFORMATION pi; 
	int  iCmdLinePos = 1,i;
	HANDLE hStdOut;

	hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

	if (GetACP() == 936) {
		setlocale(LC_ALL, "chs");
	}

	GetModuleFileName(NULL,g_szMyPath,ARRAYSIZE(g_szMyPath));

	if (_tcsstr(g_szMyPath,TEXT("..\\"))) {
		int nTextLength = (int)_tcslen(g_szMyPath);
		LPTSTR pszDir = FixPathFileNameString(g_szMyPath,nTextLength);

		if (pszDir) {
			_tcscpy(g_szMyPath,pszDir);

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

	if (_Argc < 2) {
		Usage();
		//
		return -1;
	}

	for (i=1;i <_Argc;i++){
		if (  (_Argv[i][0] == '-') || (_Argv[i][0] == '/') )	{
			if (_tcslen(_Argv[i]) == 2) {
				switch (tolower(_Argv[i][1])){
				case 'l':
					bPrintLog = 1;
					break;

				case 'w':
					bWaitExit = 1;
					break;
				case 'o':
					if (i <_Argc -1) {
						i++;
						iCmdLinePos ++;
						pszOutputFile = _Argv[i];
					}
					else {
						_tprintf(TEXT("No output file!\n"));
						Usage();
						return -1;
					}
					
					break;
				}
			}
			
			iCmdLinePos ++;
		}
		else {
			break;
		}
	}
	
	if (iCmdLinePos >= _Argc) {
		return -1;
	}
	if (!InitStdOut()) {
		return -3;
	}

	pszExePath = _Argv[iCmdLinePos];
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

	bHasSpace = _tcschr(pszExePath,' ') != NULL;

	if (bHasSpace) *pch ++ = '\"';
	_tcscpy(pch,pszExePath);
	pch += _tcslen(pszExePath);
	if (bHasSpace) *pch ++ = '\"';
	
	for (i = iCmdLinePos + 1; i < _Argc; i ++) {
		_TCHAR * argv = _Argv[i];

		*pch ++ = ' ';

		bHasSpace = _tcschr(argv,' ') != NULL;

		if (bHasSpace) *pch ++ = '\"';
		_tcscpy(pch,argv);
		pch += _tcslen(argv);
		if (bHasSpace) *pch ++ = '\"';
	}
	
	*pch = 0;
	
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si); 
	ZeroMemory(&pi, sizeof(pi));

	pszEvnVar = (LPTSTR)GetEnvironmentStrings();

	HANDLE hFileStdOut = g_hChildStd_OUT_Wr;

	if (pszOutputFile) {
		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		sa.lpSecurityDescriptor = NULL;
		
		hFileStdOut = CreateFile(pszOutputFile,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
		if (hFileStdOut == INVALID_HANDLE_VALUE) {
			_tprintf(TEXT("Create output file %s failed\n"),pszOutputFile);
			return -1;
		}
	}
	
	si.cb = sizeof(STARTUPINFO); 
	si.hStdError = hFileStdOut;//g_hChildStd_OUT_Wr;
	si.hStdOutput = hFileStdOut;//g_hChildStd_OUT_Wr;
	si.hStdInput = g_hChildStd_IN_Rd;
	si.dwFlags = STARTF_USESTDHANDLES;

	if (bPrintLog) {
		_tprintf(TEXT("Starting %s"),pszCommandLine);
	}

	bReturn = CreateProcess(NULL, pszCommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, pszEvnVar, szCurrentDirectory, &si, &pi);
	
	FreeEnvironmentStrings(pszEvnVar);
	free(pszCommandLine);

#define DEFAULT_CONSOLE_COLOR	( FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

	if (!bReturn) {
		DWORD dwError = GetLastError();

		if (bPrintLog) {
			SetConsoleTextAttribute( hStdOut, FOREGROUND_RED ); 
			_tprintf(TEXT(" Failed!"),dwError);
			SetConsoleTextAttribute( hStdOut, DEFAULT_CONSOLE_COLOR );

			_tprintf(TEXT(",Error Code:%u\n"),dwError);
		}

		return -5;
	}

	CloseHandle(pi.hThread);

	if (bPrintLog) {
		SetConsoleTextAttribute( hStdOut, FOREGROUND_GREEN ); 
		_tprintf(TEXT(" Success!\n"));
		SetConsoleTextAttribute( hStdOut, DEFAULT_CONSOLE_COLOR); 
	}

	if (bWaitExit) {
		WaitForSingleObject(pi.hProcess,INFINITE);
	}

	CloseHandle(pi.hProcess);

	if (hFileStdOut != g_hChildStd_OUT_Wr)
		CloseHandle(hFileStdOut);
	
	return 0;
}

