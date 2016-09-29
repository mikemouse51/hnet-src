
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#ifndef _W_CORE_H_
#define _W_CORE_H_

#include "wLinux.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <ctime>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#include <cstdio>

#define SAFE_NEW(type, ptr) \
do { \
       try { \
            ptr = NULL; \
            ptr = new type; \
        } catch (...) { \
            ptr = NULL; \
        } \
} while (0)

#define SAFE_NEW_VEC(n, type, ptr) \
do { \
       try { \
            ptr = NULL; \
            ptr = new type[n]; \
        } catch (...) { \
            ptr = NULL; \
        } \
} while(0)

#define SAFE_DELETE(ptr) \
do { \
       if(ptr) { \
           delete ptr; \
     ptr = NULL; \
      } \
} while(0)

#define SAFE_DELETE_VEC(ptr) \
do { \
       delete[] ptr; \
       ptr = NULL; \
} while(0)

namespace hnet {

const char      kProcTitlePad = '\0';
const char      kLF = '\n';
const char      kCR = '\r';
const char      kCRLF[] = "\r\n";

const uint32_t  kMaxHostNameLen = 255;
const uint8_t   kMaxIpLen = 16;
const uint32_t  kListenBacklog = 511;
const int32_t   kFDUnknown = -1;

const uint32_t  kKeepAliveTm = 3000;
const uint8_t   kKeepAliveCnt = 5;

const uint8_t   kHeartbeat = 5;

// 16m shm消息队列大小
const uint32_t  kMsgQueueLen = 16777216;

// 512k 客户端task消息缓冲大小
const uint32_t  kPackageSize = 524288;
const uint32_t  kMaxPackageSize = 524284;
const uint32_t  kMinPackageSize = 1;

//进程相关
const uint32_t	kMaxProcess = 1024;

const int8_t    kProcessNoRespawn = -1;		// 子进程退出时，父进程不再创建
const int8_t    kProcessJustSpawn = -2;		// 子进程正在重启，该进程创建之后，再次退出时，父进程不再创建
const int8_t    kProcessRespawn = -3;     // 子进程异常退出时，父进程会重新创建它
const int8_t    kProcessJustRespawn = -4;	// 子进程正在重启，该进程创建之后，再次退出时，父进程会重新创建它
const int8_t    kProcessDetached = -5;		// 分离进程

const bool      kLittleEndian = true;
const uint32_t  kPageSize = 4096;

// 根据具体项目修改
const char      kSoftwareName[]   = "hnet";
const char      kSoftwareVer[]    = "0.0.1";

const uid_t     kDeamonUser = 0;
const gid_t     kDeamonGroup = 0;

const char      kPidPath[] = "../log/hnet.pid";
const char      kLockPath[] = "../log/hnet.lock";
const char      kAcceptMutex[] = "../log/hnet.mutex.bin";

const char      kToken[] = "Anny";

using namespace std;

}   // namespace hnet

#endif