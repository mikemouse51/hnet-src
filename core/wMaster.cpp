
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#include "wMaster.h"
#include "wSlice.h"
#include "wMisc.h"
#include "wEnv.h"
#include "wSigSet.h"
#include "wSignal.h"
#include "wWorker.h"
#include "wChannelCmd.h" // *channel*_t
#include "wChannelSocket.h"
#include "wTask.h"
#include "wConfig.h"
#include "wServer.h"

namespace hnet {

wMaster::wMaster(std::string title, wServer* server, wConfig* config) : mEnv(wEnv::Default()), mServer(server), mConfig(config), 
mTitle(title), mPid(getpid()), mSlot(kMaxProcess), mDelay(0), mSigio(0), mLive(1) {
    // pid文件路径
    std::string pid_path;
	if (mConfig->GetConf("pid_path", &pid_path) && pid_path.size() > 0) {
		mPidPath = pid_path;
	} else {
		mPidPath = kPidPath;
	}
	mWorkerNum = mNcpu = sysconf(_SC_NPROCESSORS_ONLN);
	memset(mWorkerPool, 0, sizeof(mWorkerPool));
}

wMaster::~wMaster() {
    for (uint32_t i = 0; i < kMaxProcess; ++i) {
		SAFE_DELETE(mWorkerPool[i]);
    }
}

wStatus wMaster::PrepareStart() {
    // 检测配置、服务实例
    if (mServer == NULL) {
    	return mStatus = wStatus::IOError("wMaster::PrepareStart failed", "mServer is null");
    }

    std::string host;
    int16_t port = 0;
    if (mConfig == NULL || !mConfig->GetConf("host", &host) || !mConfig->GetConf("port", &port)) {
    	return mStatus = wStatus::IOError("wMaster::PrepareStart failed", "mConfig is null or host|port is illegal");
    }

    mStatus = PrepareRun();
    if (!mStatus.Ok()) {
    	return mStatus;
    }
    mStatus = mConfig->Setproctitle(kMasterTitle, mTitle.c_str());
    if (!mStatus.Ok()) {
    	return mStatus;
    }

    std::string protocol;
    if (!mConfig->GetConf("protocol", &protocol)) {
    	mStatus = mServer->PrepareStart(host, port);
    } else {
    	mStatus = mServer->PrepareStart(host, port, protocol);
    }
    return mStatus;
}

wStatus wMaster::SingleStart() {
    if (!CreatePidFile().Ok()) {
		return mStatus;
    } else if (!InitSignals().Ok()) {
        return mStatus;
    }
    
    mStatus = Run();
    if (!mStatus.Ok()) {
    	return mStatus;
    }
    return mStatus = mServer->SingleStart();
}

wStatus wMaster::MasterStart() {
    if (mWorkerNum > kMaxProcess) {
        return mStatus = wStatus::IOError("wMaster::MasterStart, processes can be spawned", "worker number is overflow");
    }

    if (!CreatePidFile().Ok()) {
		return mStatus;
    } else if (!InitSignals().Ok()) {
        return mStatus;
    }

    // 信号阻塞
    wSigSet ss;
    ss.AddSet(SIGCHLD);
    ss.AddSet(SIGALRM);
    ss.AddSet(SIGIO);
    ss.AddSet(SIGQUIT);	// 立即退出
    ss.AddSet(SIGINT);	// 优雅退出
    ss.AddSet(SIGTERM);	// 优雅退出
    ss.AddSet(SIGHUP);	// 重新读取配置
    ss.AddSet(SIGUSR1);	// 重启服务
    mStatus = ss.Procmask();
    if (!mStatus.Ok()) {
        return mStatus;
    }

    // 初始化进程表
	for (uint32_t i = 0; i < kMaxProcess; i++) {
		if (!NewWorker(i, &mWorkerPool[i]).Ok()) {
			return mStatus;
		}
	}

    // 启动worker工作进程
    if (!WorkerStart(mWorkerNum, kProcessRespawn).Ok()) {
    	return mStatus;
    }

    // 主进程监听信号
    while (true) {
		HandleSignal();
		Run();
    }
}

wStatus wMaster::HandleSignal() {
	if (mDelay) {
		if (g_sigalrm) {
			mSigio = 0;
			mDelay *= 2;
			g_sigalrm = 0;
		}

		struct itimerval itv;
		itv.it_interval.tv_sec = 0;
		itv.it_interval.tv_usec = 0;
		itv.it_value.tv_sec = mDelay / 1000;
		itv.it_value.tv_usec = (mDelay % 1000 ) * 1000;

		// 设置定时器，以系统真实时间来计算，送出SIGALRM信号（主要用户优雅退出）
		if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
			mStatus = wStatus::IOError("wMaster::HandleSignal, setitimer() failed", strerror(errno));
		}
	}

	// 阻塞方式等待信号量。会被上面设置的定时器打断
	wSigSet ss;
	ss.Suspend();
	
	// SIGCHLD
	if (g_reap) {
		g_reap = 0;
		
		// waitpid
		WorkerExitStat();
		// 有worker退出，重启
		mStatus = ReapChildren();
	}
	
	// worker都退出，且收到了退出信号，则master退出
	if (!mLive && (g_terminate || g_quit)) {
		ProcessExit();
	    DeletePidFile();
	    exit(0);
	}
	
	// SIGTERM、SIGINT
	if (g_terminate) {
		// 设置延时，50ms
		if (mDelay == 0) {
			mDelay = 50;
		}
		if (mSigio > 0) {
			mSigio--;
			return mStatus;
		}
		mSigio = mWorkerNum;

		// 通知所有worker退出，并且等待worker退出
		if (mDelay > 1000) {
			// 延时已到（最大1s），给所有worker发送SIGKILL信号，强制杀死worker
			SignalWorker(SIGKILL);
		} else {
			// 给所有worker发送SIGTERM信号，通知worker优雅退出
			SignalWorker(SIGTERM);
		}
		return mStatus;
	}

	// SIGQUIT
	if (g_quit) {
		// 给所有的worker发送SIGQUIT信号
		SignalWorker(SIGQUIT);
		return mStatus;
	}
	
	// SIGHUP
	if (g_reconfigure) {
		g_reconfigure = 0;
		
		// 重新初始化主进程配置
		Reload();
		
		// 重启worker
		WorkerStart(mWorkerNum, kProcessJustRespawn);
		
		// 100ms
		usleep(100*1000);
		
		mLive = 1;
		// 关闭原来worker进程
		SignalWorker(SIGTERM);
	}
	return mStatus;
}

wStatus wMaster::Reload() {
	// todo
	return mStatus;
}

wStatus wMaster::ReapChildren() {
	mLive = 0;
	for (uint32_t i = 0; i < kMaxProcess; i++) {
        if (mWorkerPool[i] == NULL || mWorkerPool[i]->mPid == -1) {
            continue;
        }

        // 进程已退出
		if (mWorkerPool[i]->mExited) {
			if (!mWorkerPool[i]->mDetached) {
				// 非分离进程同步文件描述符
				struct ChannelReqClose_t ch;
				ch.mFD = -1;
				ch.mSlot = i;
				ch.mPid = mWorkerPool[i]->mPid;
				PassCloseChannel(&ch);

				// 关闭channel
				mWorkerPool[i]->Channel()->Close();
			}
			
			// 重启worker
			if (mWorkerPool[i]->mRespawn && !mWorkerPool[i]->mExiting && !g_terminate && !g_quit) {
				if (!SpawnWorker(i).Ok()) {
					continue;
				}
				
				// 同步文件描述符
				struct ChannelReqOpen_t ch;
				ch.mFD = mWorkerPool[mSlot]->ChannelFD(0);
				ch.mPid = mWorkerPool[mSlot]->mPid;
				ch.mSlot = i;
				PassOpenChannel(&ch);

				mLive = 1;
				continue;
			}
		} else if (mWorkerPool[i]->mExiting || !mWorkerPool[i]->mDetached) {
			mLive = 1;
		}
    }

    return mStatus;
}

wStatus wMaster::WorkerStart(uint32_t n, int32_t type) {
	for (uint32_t i = 0; i < n; ++i) {
		if (!SpawnWorker(type).Ok()) {
			return mStatus;
		}
		struct ChannelReqOpen_t opench;
		opench.mFD = mWorkerPool[mSlot]->ChannelFD(0);
        opench.mPid = mWorkerPool[mSlot]->mPid;
        opench.mSlot = mSlot;
        PassOpenChannel(&opench);
	}
	return mStatus;
}

void wMaster::SignalWorker(int signo) {
	struct ChannelReqQuit_t chopen;
	struct ChannelReqTerminate_t chclose;
	struct ChannelReqCmd_s* ch = NULL;

	int other = 0;
	size_t size = 0;
	switch (signo) {
	case SIGQUIT:
		ch = reinterpret_cast<ChannelReqCmd_s*>(&chopen);
		ch->mFD = -1;
		size = sizeof(chopen);
		break;
			
	case SIGTERM:
		ch = reinterpret_cast<ChannelReqCmd_s*>(&chclose);
		ch->mFD = -1;
		size = sizeof(chclose);
		break;

	default:
		other = 1;
	}

	char *ptr = NULL;
	if (!other) {
		// wcommand消息长度
		SAFE_NEW_VEC(size + sizeof(uint8_t) + sizeof(uint32_t), char, ptr);
	}
	
	for (uint32_t i = 0; i < kMaxProcess; i++) {
		// 分离进程
        if (mWorkerPool[i] == NULL || mWorkerPool[i]->mDetached || mWorkerPool[i]->mPid == -1) {
        	continue;
        }
        // 进程正在重启
        if (mWorkerPool[i]->mJustSpawn) {
        	mWorkerPool[i]->mJustSpawn = 0;
        	continue;
        }
        // 正在强制退出
		if (mWorkerPool[i]->mExiting && signo == SIGQUIT) {
			continue;
		}
		
        if (!other) {
	        /* TODO: EAGAIN */
        	ssize_t ret;
        	wTask::Assertbuf(ptr, reinterpret_cast<char*>(ch), size);
			if (mWorkerPool[i]->Channel()->SendBytes(ptr, size + sizeof(uint8_t) + sizeof(uint32_t), &ret).Ok()) {
				if (signo == SIGQUIT || signo == SIGTERM) {
					mWorkerPool[i]->mExiting = 1;
				}
				continue;
			}
		}

        if (kill(mWorkerPool[i]->mPid, signo) == -1) {            
            if (errno == ESRCH) {
                mWorkerPool[i]->mExited = 1;
                mWorkerPool[i]->mExiting = 0;
				
                g_reap = 1;
            }
            continue;
        }
		
		// 非重启服务
        if (signo != SIGUSR1) {
        	mWorkerPool[i]->mExiting = 1;
        }
    }
    
    if (!other) {
    	SAFE_DELETE_VEC(ptr);
    }
}

void wMaster::PassOpenChannel(struct ChannelReqOpen_t *ch) {
	char *ptr;
	size_t size = sizeof(struct ChannelReqOpen_t);
	SAFE_NEW_VEC(size + sizeof(uint32_t) + sizeof(uint8_t), char, ptr);

	ssize_t ret;
	for (uint32_t i = 0; i < kMaxProcess; i++) {
		// 无需发送给自己
        if (i == mSlot || mWorkerPool[i] == NULL || mWorkerPool[i]->mPid == -1) {
        	continue;
        }

        /* TODO: EAGAIN */
		wTask::Assertbuf(ptr, reinterpret_cast<char*>(ch), size);
		mWorkerPool[i]->Channel()->SendBytes(ptr, sizeof(uint32_t) + sizeof(uint8_t) + size, &ret);
    }
    SAFE_DELETE_VEC(ptr);
}

void wMaster::PassCloseChannel(struct ChannelReqClose_t *ch) {
	char *ptr;
	size_t size = sizeof(struct ChannelReqClose_t);
	SAFE_NEW_VEC(size + sizeof(uint32_t) + sizeof(uint8_t), char, ptr);

    ssize_t ret;
	for (uint32_t i = 0; i < kMaxProcess; i++) {
		// 不发送已退出worker
		if (mWorkerPool[i] == NULL || mWorkerPool[i]->mExited || mWorkerPool[i]->mPid == -1) {
			continue;
		}
        
        /* TODO: EAGAIN */
		wTask::Assertbuf(ptr, reinterpret_cast<char*>(ch), size);
		mWorkerPool[i]->Channel()->SendBytes(ptr, sizeof(uint32_t) + sizeof(uint8_t) + size, &ret);
    }
    SAFE_DELETE_VEC(ptr);
}

wStatus wMaster::NewWorker(uint32_t slot, wWorker** ptr) {
    SAFE_NEW(wWorker(mTitle, slot, this), *ptr);
    if (*ptr == NULL) {
		return mStatus = wStatus::IOError("wMaster::NewWorker", "new failed");
    }
    return mStatus;
}

wStatus wMaster::SpawnWorker(int64_t type) {
	if (type >= 0) {
		// 启动指定索引worker进程
		mSlot = static_cast<uint32_t>(type);
	} else {
		uint32_t idx;
		for (idx = 0; idx < kMaxProcess; ++idx) {
			if (mWorkerPool[idx] == NULL || mWorkerPool[idx]->mPid == -1) {
				break;
			}
		}
		mSlot = idx;
	}

	if (mSlot >= kMaxProcess) {
		return mStatus = wStatus::IOError("wMaster::SpawnWorker failed", "mSlot overflow");
	}

	// 新建|覆盖 进程表项
	if (mWorkerPool[mSlot] == NULL) {
		if (!NewWorker(mSlot, &mWorkerPool[mSlot]).Ok()) {
			return mStatus;
		}
	}
	wWorker *worker = mWorkerPool[mSlot];
	
	// 打开进程间IPC通信channel
	mStatus = worker->Channel()->Open();
	if (!mStatus.Ok()) {
		return mStatus;
	}

	// 设置第一个描述符的异步IO通知机制
	// FIOASYNC现已被O_ASYNC标志位取代
	// todo
	u_long on = 1;
    if (ioctl(worker->ChannelFD(0), FIOASYNC, &on) == -1) {
		// 关闭channel
    	worker->Channel()->Close();
    	return mStatus = wStatus::IOError("wMaster::SpawnWorker, ioctl(FIOASYNC) failed", strerror(errno));
    }
    // 设置将要在文件描述符channel[0]上接收SIGIO 或 SIGURG事件信号的进程标识
    if (fcntl(worker->ChannelFD(0), F_SETOWN, mPid) == -1) {
    	worker->Channel()->Close();
    	return mStatus = wStatus::IOError("wMaster::SpawnWorker, fcntl(F_SETOWN) failed", strerror(errno));
    }

    pid_t pid = fork();
    switch (pid) {
    case -1:
    	worker->Channel()->Close();
        return mStatus = wStatus::IOError("wMaster::SpawnWorker, fork() failed", strerror(errno));

    case 0:
    	// worker进程
        worker->mPid = getpid();
        mStatus = worker->Prepare();
        if (!mStatus.Ok()) {
        	exit(2);
        }
        // 进入worker主循环
        mStatus = worker->Start();
        exit(0);
    }

    // 主进程master更新进程表
    worker->mSlot = mSlot;
    worker->mPid = pid;
	worker->mExited = 0;
	worker->mExiting = 0;
	
	if (type >= 0) {
		return mStatus;
	}

    switch (type) {
	case kProcessNoRespawn:
		worker->mRespawn = 0;
		worker->mJustSpawn = 0;
		worker->mDetached = 0;
		break;

	case kProcessRespawn:
		worker->mRespawn = 1;
		worker->mJustSpawn = 0;
		worker->mDetached = 0;
		break;
		
	case kProcessJustSpawn:
		worker->mRespawn = 0;
		worker->mJustSpawn = 1;
		worker->mDetached = 0;

	case kProcessJustRespawn:
		worker->mRespawn = 1;
		worker->mJustSpawn = 1;
		worker->mDetached = 0;

	case kProcessDetached:
		worker->mRespawn = 0;
		worker->mJustSpawn = 0;
		worker->mDetached = 1;
    }
	
    return mStatus;
}

wStatus wMaster::CreatePidFile() {
	string pidstr = logging::NumberToString(mPid);
	return mStatus = WriteStringToFile(mEnv, pidstr, mPidPath);
}

wStatus wMaster::DeletePidFile() {
	return mStatus = mEnv->DeleteFile(mPidPath);
}

wStatus wMaster::SignalProcess(char* sig) {
	std::string str;
	mStatus = ReadFileToString(mEnv, mPidPath, &str);
	if (!mStatus.Ok()) {
		return mStatus;
	}

	uint64_t pid = 0;
	if (logging::ConsumeDecimalNumber(&str, &pid) && pid > 0) {
		for (wSignal::Signal_t* s = g_signals; s->mSigno != 0; ++s) {
			if (strcmp(sig, s->mName)) {
				if (kill(pid, s->mSigno) == -1) {
					return wStatus::IOError("wMaster::SignalProcess, kill() failed", strerror(errno));
				} else {
					return wStatus::Nothing();
				}
			}
		}
	}
	return wStatus::IOError("wMaster::SignalProcess, signal failed", "cannot find signal");
}

wStatus wMaster::InitSignals() {
	wSignal signal;
	for (wSignal::Signal_t* s = g_signals; s->mSigno != 0; ++s) {
		mStatus = signal.AddHandler(s);
		if (!mStatus.Ok()) {
			return mStatus;
		}
	}
	return mStatus;
}

void wMaster::WorkerExitStat() {
    while (true) {
		int status, one = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid == 0) {
            return;
        } else if (pid == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ECHILD && one) {
                return;
            } else if (errno == ECHILD) {
                return;
            }
            return;
        }
		
        one = 1;
		uint32_t i;
		for (i = 0; i < kMaxProcess; ++i) {
			if (mWorkerPool[i] != NULL && mWorkerPool[i]->mPid == pid) {
                mWorkerPool[i]->mStat = status;
                mWorkerPool[i]->mExited = 1;
                break;
			}
		}

		/*
        if (WTERMSIG(status)) {
			LOG_ERROR(ELOG_KEY, "[system] %s %d exited on signal %d%s", process, pid, WTERMSIG(status), WCOREDUMP(status) ? " (core dumped)" : "");
        } else {
			LOG_ERROR(ELOG_KEY, "[system] %s %d exited with code %d", process, pid, WTERMSIG(status));
        }
		*/
	
		// 退出码为2时，退出后不重启
        if (WEXITSTATUS(status) == 2 && mWorkerPool[i]->mRespawn) {
			//LOG_ERROR(ELOG_KEY, "[system] %s %d exited with fatal code %d and cannot be respawned", process, pid, WTERMSIG(status));
            mWorkerPool[i]->mRespawn = 0;
        }
    }
}

}	// namespace hnet
