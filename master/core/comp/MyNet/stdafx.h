// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件
//

#ifndef _klib_stdafx_h
#define _klib_stdafx_h

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             //  从 Windows 头文件中排除极少使用的信息
// Windows 头文件:
#include <windows.h>



// TODO: 在此处引用程序需要的其他头文件
#include <WinSock2.h>
#include <tchar.h>
#include <crtdbg.h>
#include <atltrace.h>

#include <klib_link.h>

#include <kthread/auto_lock.h>
using namespace klib::kthread;

#endif


#include "../mem_check/library/src/mem_lib.h"
#include "../mem_check/library/src/redefine_new.h"