
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#include <sys/un.h>
#include <poll.h>
#include "wUnixSocket.h"
#include "wMisc.h"
#include "wEnv.h"

namespace hnet {

wUnixSocket::~wUnixSocket() {
	wEnv::Default()->DeleteFile(mHost);
}

const wStatus& wUnixSocket::Open() {
	if ((mFD = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		return mStatus = wStatus::IOError("wUnixSocket::Open socket() AF_UNIX failed", error::Strerror(errno));
	}
	return mStatus.Clear();
}

const wStatus& wUnixSocket::Bind(const std::string& host, uint16_t port) {
	struct sockaddr_un socketAddr;
	memset(&socketAddr, 0, sizeof(socketAddr));
	socketAddr.sun_family = AF_UNIX;
	strncpy(socketAddr.sun_path, host.c_str(), sizeof(socketAddr.sun_path) - 1);
	if (bind(mFD, reinterpret_cast<struct sockaddr *>(&socketAddr), sizeof(socketAddr)) == -1) {
		return mStatus = wStatus::IOError("wUnixSocket::Bind bind failed", error::Strerror(errno));
	}
	return mStatus.Clear();
}

const wStatus& wUnixSocket::Listen(const std::string& host, uint16_t port) {
	mHost = host;
	mPort = port;
	if (!Bind(mHost, mPort).Ok()) {
		return mStatus;
	}

	if (listen(mFD, kListenBacklog) < 0) {
		return mStatus = wStatus::IOError("wUnixSocket::Listen listen failed", error::Strerror(errno));
	}
	return SetFL();
}

const wStatus& wUnixSocket::Connect(int64_t *ret, const std::string& host, uint16_t port, float timeout) {
	// 客户端host、port
	mPort = 0;
	char filename[PATH_MAX];
	snprintf(filename, PATH_MAX, "%s%d%s", kUnixSockPrefix, static_cast<int>(getpid()), ".sock");
	mHost = filename;

	if (!Bind(mHost).Ok()) {
		*ret = static_cast<int64_t>(-1);
		return mStatus;
	}
	
	// 超时设置
	if (timeout > 0) {
		if (!SetFL().Ok()) {
			*ret = -1;
			return mStatus;
		}
	}
	
	struct sockaddr_un socketAddr;
	memset(&socketAddr, 0, sizeof(socketAddr));
	socketAddr.sun_family = AF_UNIX;
	strncpy(socketAddr.sun_path, host.c_str(), sizeof(socketAddr.sun_path) - 1);
	*ret = static_cast<int64_t>(connect(mFD, reinterpret_cast<const struct sockaddr *>(&socketAddr), sizeof(socketAddr)));
	if (*ret == -1 && timeout > 0) {
		// 建立启动但是尚未完成
		if (errno == EINPROGRESS) {
			while (true) {
				struct pollfd pfd;
				pfd.fd = mFD;
				pfd.events = POLLIN | POLLOUT;
				int rt = poll(&pfd, 1, timeout * 1000000);	// 微妙
				if (rt == -1) {
					if (errno == EINTR) {
					    continue;
					}
					return mStatus = wStatus::IOError("wUnixSocket::Connect poll failed", error::Strerror(errno));
				} else if(rt == 0) {
					return mStatus = wStatus::IOError("wUnixSocket::Connect connect timeout", "");
				} else {
					int error, len = sizeof(int);
					int code = getsockopt(mFD, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), reinterpret_cast<socklen_t*>(&len));
					if (code == -1) {
					    return mStatus = wStatus::IOError("wUnixSocket::Connect getsockopt SO_ERROR failed", error::Strerror(errno));
					}
					if (error != 0) {
						errno = error;
					    return mStatus = wStatus::IOError("wUnixSocket::Connect connect failed", error::Strerror(errno));
					}
					// 连接成功
					*ret = 0;
					break;
				}
			}
		} else {
			return mStatus = wStatus::IOError("wUnixSocket::Connect connect directly failed", error::Strerror(errno));
		}
	}
	return mStatus;
}

const wStatus& wUnixSocket::Accept(int64_t *fd, struct sockaddr* clientaddr, socklen_t *addrsize) {
	if (mSockType != kStListen) {
		*fd = -1;
		return mStatus = wStatus::InvalidArgument("wUnixSocket::Accept", "is not listen socket");
	}
	
	while (true) {
		*fd = static_cast<int64_t>(accept(mFD, clientaddr, addrsize));
		if (*fd > 0) {
			break;
		} else if (errno == EAGAIN) {
			continue;
		} else if (errno == EINTR) {
            // 操作被信号中断，中断后唤醒继续处理
            // 注意：系统中信号安装需提供参数SA_RESTART，否则请按 EAGAIN 信号处理
			continue;
		} else {
			mStatus = wStatus::IOError("wUnixSocket::Accept, accept failed", error::Strerror(errno));
			break;
		}
	}

	if (!mStatus.Ok() || *fd <= 0) {
		mStatus = wStatus::IOError("wUnixSocket::Accept accept() failed", "");
	}
	return mStatus;
}

}	// namespace hnet
