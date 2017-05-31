
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#ifndef _W_PROC_TITLE_H_
#define _W_PROC_TITLE_H_

#include "wCore.h"
#include "wNoncopyable.h"

namespace hnet {

class wProcTitle : private wNoncopyable {
public:
    wProcTitle() { }
    ~wProcTitle();

    // 务必在设置进程标题之前调用
    // 移动**argv到堆上，移动**environ到堆上
    // tips：*argv[]与**environ两个变量所占的内存是连续的，并且是**environ紧跟在*argv[]后面
    int SaveArgv(int argc, const char* argv[]);

    // 设置进程标题
    int Setproctitle(const char *title, const char *pretitle = NULL, bool attach = true);

protected:
    const char** mOsArgv;
    int mArgc;
    char** mArgv;
    int mNumEnv;
    char** mEnv;
};

}	// namespace hnet

#endif
