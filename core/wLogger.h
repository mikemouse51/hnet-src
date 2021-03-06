
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#ifndef _W_LOGGER_H_
#define _W_LOGGER_H_

#include <map>
#include <cstdarg>
#include "wCore.h"
#include "wNoncopyable.h"

#ifdef _USE_LOGGER_
#	define  HNET_ERROR(logpath, fmt, ...) hnet::Logv(logpath, fmt, ##__VA_ARGS__)
#	ifndef _DEBUG_
#		define  HNET_DEBUG(logpath, fmt, ...)
#	else
#		define  HNET_DEBUG(logpath, fmt, ...) hnet::Logv(logpath, fmt, ##__VA_ARGS__)
#	endif
#else
#	define  HNET_ERROR(logpath, fmt, ...)
#	define  HNET_DEBUG(logpath, fmt, ...)
#endif

namespace hnet {

// 文件名：error.log(\.[0-9]+)?
// 日志接口类，非线程安全，对外接口需加锁
class wLogger : private wNoncopyable {
public:
	wLogger() { }
    virtual ~wLogger() { }

    // 写一条指定格式的日志到文件中
    virtual void Logv(const char* format, va_list ap) = 0;
};

// 实现类
// 日志实现类
class wPosixLogger : public wLogger {
public:
	wPosixLogger(const std::string& fname, uint64_t (*getpid)(), off_t maxsize = kMaxLoggerSize);

	virtual ~wPosixLogger() {
		CloseLog(mFile);
	}

	// 写日志。最大3000字节，多的截断
	virtual void Logv(const char* format, va_list ap);

private:
	void ArchiveLog();

	inline FILE* OpenCreatLog(const std::string& fname, const std::string& mode) {
		return fopen(fname.c_str(), mode.c_str());
	}

	inline void CloseLog(FILE* f) {
		fclose(f);
	}

	FILE* mFile;
	std::string mFname;
	off_t mMaxsize;
	uint64_t (*mGetpid)();	// 获取当前进程id函数指针
};

// 写log接口
void Logv(const std::string& logpath, const char* format, ...);

// 释放log接口
void Logd();

}	// namespace hnet

#endif
