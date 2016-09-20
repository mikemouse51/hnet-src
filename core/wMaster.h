
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#ifndef _W_MASTER_H_
#define _W_MASTER_H_

#include <map>
#include <vector>
#include "wCore.h"
#include "wStatus.h"
#include "wNoncopyable.h"

namespace hnet {

const char* kMasterTitle = " - master process";

class wEnv;
class wServer;
class wConfig;
class wWorker;

class wMaster : private wNoncopyable {
public:
	// title进程标题前缀
    wMaster(char* title, wServer* server, wConfig* config);

    virtual ~wMaster();
    
    wStatus Prepare();
    
    // 单进程模式启动
    wStatus SingleStart();

    // m-w模式启动(master-worker)
    wStatus MasterStart();

	// 修改pid文件名（默认hnet.pid）
	// 修改启动worker个数（默认cpu个数）
	// 修改自定义信号处理（默认定义在wSignal.cpp文件中）
	virtual wStatus PrepareRun() {
		return mStatus = wStatus::IOError("wMaster::PrepareRun failed", "method should be inherit");
	}

	virtual wStatus Run() {
		return mStatus = wStatus::IOError("wMaster::Run failed", "method should be inherit");
	}
    
    virtual wStatus NewWorker(uint32_t slot, wWorker** ptr);
    virtual wStatus HandleSignal();

	virtual wStatus ReloadMaster() {
		return mStatus = wStatus::IOError("wMaster::ReloadMaster failed", "method should be inherit");
	}

protected:
	friend class wWorker;

    // 启动n个worker进程
    wStatus WorkerStart(uint32_t n, int8_t type = kPorcessRespawn);
    // 创建一个worker进程
    wStatus SpawnWorker(int8_t type = kPorcessRespawn);

    // 向所有已启动worker传递刚启动worker的channel描述符
    void PassOpenChannel(struct ChannelReqOpen_t *ch);
    // 向所有已启动worker传递关闭worker的channel消息
    void PassCloseChannel(struct ChannelReqClose_t *ch);
    // 给所有worker进程发送信号
    void SignalWorker(int signo);

    // 如果有worker异常退出，则重启
    // 如果所有的worker都退出了，则返回0
    wStatus ReapChildren(int* live);

	wStatus CreatePidFile();
	wStatus DeletePidFile();
	
	// 注册信号回调
	// 可覆盖全局变量g_signals，实现自定义信号处理
	wStatus InitSignals();

	// 回收退出进程状态（waitpid以防僵尸进程）
	// 更新进程表
	void ProcessExitStat();
    
    void SingleExit();
    void MasterExit();

	wStatus mStatus;
	uint8_t mNcpu;
	pid_t mPid;		// master进程id
	const string mPidPath;
	const string mTitle;	// 进程名
	
	// 进程表
	uint32_t mWorkerNum; // worker数量
	uint32_t mSlot;		// worker分配索引
	wWorker *mWorkerPool[kMaxPorcess];

	wEnv *mEnv;
	wServer* mServer;
	wConfig* mConfig;
};

}	// namespace hnet

#endif