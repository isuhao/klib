#pragma once

#pragma warning(disable: 4996)

#include <stdio.h>
#include <time.h>
#include <locale>
#include <windows.h>
#include <TCHAR.H>

//#define RELEASE_DEBUG

namespace klib {
namespace debug {

#ifndef _countof
#define _countof(Arr)  (sizeof(Arr)/sizeof(Arr[0]))
#endif

#if defined(_DEBUG) || defined(RELEASE_DEBUG)


/**
 * @brief ��־��ӡ��
 * @usage  
 *
    MyPrtLog("%s", "info");
 */
class CPrtLogFunc
{
public:
  const char * m_file;
  int m_line;

  CPrtLogFunc(const char * file,int line)
  {
    m_file = file;
    m_line = line;
  }

  bool get_app_path(char path[], int pathlen)
  {
      DWORD dwlen = GetModuleFileNameA(GetModuleHandle(NULL), path, pathlen);
      int index = dwlen - 1;
      while (index > 0 )
      {
          if (path[index] == '\\') {
              path[index + 1] = '\0';
              break;
          }
          -- index;
      }
      return true;
  }

  bool get_app_path(WCHAR path[], int pathlen)
  {
      DWORD dwlen = GetModuleFileNameW(GetModuleHandle(NULL), path, pathlen);
      int index = dwlen - 1;
      while (index > 0)
      {
          if (path[index] == _T('\\')) {
              path[index + 1] = _T('\0');
              break;
          }
          -- index;
      }
      return true;
  }

  void operator ()(const char * format,...)
  {
    char newformat[512] = {0};
    char msgbuffer[4*1024] = {0};
    //sprintf_s(newformat, "%s line %d,", m_file, m_line);
    //strcat_s(newformat, format);
    strcpy(newformat, format);
    va_list arg_ptr;
    va_start(arg_ptr, format);
    _vsnprintf(msgbuffer, sizeof(msgbuffer), newformat, arg_ptr);
    va_end(arg_ptr);

    //д�뵽�ļ���
    time_t long_time;
    char timebuf[26];

    time( &long_time );
    tm* newtime = localtime(&long_time );

    //�õ����ڵ��ַ���
    char file_name[1024];
    char logfile[1024] = {0};

    get_app_path(file_name, _countof(file_name));
    sprintf(logfile,
              "%s%d-%d-%d_log.txt",
              file_name,
              newtime->tm_year + 1900,
              newtime->tm_mon + 1,
              newtime->tm_mday);

    FILE* fp = NULL;
    fp = fopen(logfile, "a");
    if(fp == NULL) {
	  return;
	}

    char logMsg[4*1024 + 30] = {0};
    sprintf(timebuf, 
              "%d-%d-%d %d:%d:%d ",
              newtime->tm_year + 1900,
              newtime->tm_mon + 1,
              newtime->tm_mday,
              newtime->tm_hour,
              newtime->tm_min,
              newtime->tm_sec);
    sprintf(logMsg, "%s: %s \r\n", timebuf, msgbuffer);
    fprintf(fp, "%s", logMsg);
    fclose(fp);

    DWORD dwWritten = 0; 
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), logMsg, ::strlen(logMsg), &dwWritten, NULL); 
  }
  
  void operator()(TCHAR * format, ...) 
  {
      static BOOL bSetLocal = FALSE;
      TCHAR newformat[512] = {0};
      TCHAR msgbuffer[4*1024] = {0};
      //sprintf_s(newformat, "%s line %d,", m_file, m_line);
      //strcat_s(newformat, format);
      if (!bSetLocal) 
      {
          bSetLocal = TRUE;
          setlocale(LC_ALL, "chs");
      }

      _tcscpy(newformat, format);
      va_list arg_ptr;
      va_start(arg_ptr, format);
      _vstprintf(msgbuffer, newformat, arg_ptr);
      va_end(arg_ptr);

      //д�뵽�ļ���
      time_t long_time;
      TCHAR timebuf[26];

      time( &long_time );
      tm* newtime = localtime(&long_time );

      //�õ����ڵ��ַ���
      TCHAR file_name[1024];
      TCHAR logfile[1024] = {0};

      get_app_path(file_name, _countof(file_name));
      _stprintf(logfile,
          _T("%s%d-%d-%d_log.txt"),
          file_name,
          newtime->tm_year + 1900,
          newtime->tm_mon + 1,
          newtime->tm_mday);

      FILE* fp = NULL;
      fp = _tfopen(logfile, _T("a"));
      if(fp == NULL) {
          return;
      }

      TCHAR logMsg[4*1024 + 30] = {0};
      _stprintf(timebuf, 
          _T("%d-%d-%d %d:%d:%d "),
          newtime->tm_year + 1900,
          newtime->tm_mon + 1,
          newtime->tm_mday,
          newtime->tm_hour,
          newtime->tm_min,
          newtime->tm_sec);
      _stprintf(logMsg,  _T("%s: %s \r\n"), timebuf, msgbuffer);
      _ftprintf(fp, _T("%s"), logMsg);
      fclose(fp);

      DWORD dwWritten = 0; 
      ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), logMsg, _tcslen(logMsg), &dwWritten, NULL); 
  }
};

#define MyPrtLog CPrtLogFunc(__FILE__,__LINE__)




#else

#define MyPrtLog 

#endif

//----------------------------------------------------------------------
// ���Դ���
class CDebugConsole
{
public:
    static CDebugConsole* GetInstance()
    {
        static CDebugConsole* _instance = new CDebugConsole;
        return _instance;
    }

    ~CDebugConsole(void)
    {
        fclose(stderr);
        fclose(stdout);
        fclose(stdin);
        FreeConsole();
    }

private:
    CDebugConsole(void)
    {
        AllocConsole();   
        SetConsoleTitle(_T("Debug Console"));
        freopen("conin$",  "r+t", stdin);   
        freopen("conout$", "w+t", stdout);   
        freopen("conout$", "w+t", stderr);   
    }
    CDebugConsole(const CDebugConsole&);
    CDebugConsole& operator=(const CDebugConsole&);
};


}}