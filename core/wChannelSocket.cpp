
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#include <sys/un.h>
#include <sys/uio.h>
#include "wLogger.h"
#include "wMisc.h"
#include "wTask.h"
#include "wChannelSocket.h"
#include "wChannelCmd.h"

namespace hnet {

wChannelSocket::~wChannelSocket() {
    Close();
}

const wStatus& wChannelSocket::Open() {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, mChannel) == -1) {
        return mStatus = wStatus::IOError("wChannelSocket::Open socketpair() AF_UNIX failed", error::Strerror(errno));
    }
    
    if (fcntl(mChannel[0], F_SETFL, fcntl(mChannel[0], F_GETFL) | O_NONBLOCK) == -1) {
        return mStatus = wStatus::IOError("wChannelSocket::Open [0] fcntl() O_NONBLOCK failed", error::Strerror(errno));
    } else if (fcntl(mChannel[1], F_SETFL, fcntl(mChannel[1], F_GETFL) | O_NONBLOCK) == -1) {
        return mStatus = wStatus::IOError("wChannelSocket::Open [1] fcntl() O_NONBLOCK failed", error::Strerror(errno));
    }
    
    if (fcntl(mChannel[0], F_SETFD, FD_CLOEXEC) == -1) {
    	LOG_DEBUG(soft::GetLogPath(), "%s : %s", "wChannelSocket::Open [0] fcntl() FD_CLOEXEC failed", error::Strerror(errno).c_str());
    } else if (fcntl(mChannel[1], F_SETFD, FD_CLOEXEC) == -1) {
    	LOG_DEBUG(soft::GetLogPath(), "%s : %s", "wChannelSocket::Open [1] fcntl() FD_CLOEXEC failed", error::Strerror(errno).c_str());
    }

    // mChannel[1]被监听（可读事件）
    mFD = mChannel[1];
    return mStatus;
}

const wStatus& wChannelSocket::Close() {
	close(mChannel[0]);
	close(mChannel[1]);
	mFD = kFDUnknown;
    return mStatus;
}

const wStatus& wChannelSocket::SendBytes(char buf[], size_t len, ssize_t *size) {
    mSendTm = soft::TimeUsec();
    // msghdr.msg_control 缓冲区必须与 cmsghdr 结构对齐
    union {
        struct cmsghdr  cm;
        char space[CMSG_SPACE(sizeof(int32_t))];
    } cmsg;
    
    struct msghdr msg;

    // 数据协议
    uint8_t sp = static_cast<uint8_t>(coding::DecodeFixed8(buf + sizeof(uint32_t)));
    if (sp == kMpProtobuf) {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
    } else if (sp == kMpCommand) {
        struct wCommand *cmd = reinterpret_cast<struct wCommand*>(buf + sizeof(uint32_t) + sizeof(uint8_t));
        if (cmd->GetId() == CmdId(CMD_CHANNEL_REQ, CHANNEL_REQ_OPEN)) {
            wChannelReqOpen_t open;
            open.ParseFromArray(buf + sizeof(uint32_t) + sizeof(uint8_t), len - sizeof(uint32_t) - sizeof(uint8_t));

            msg.msg_control = reinterpret_cast<caddr_t>(&cmsg);
            msg.msg_controllen = sizeof(cmsg);
            memset(&cmsg, 0, sizeof(cmsg));

            cmsg.cm.cmsg_level = SOL_SOCKET;
            cmsg.cm.cmsg_type = SCM_RIGHTS; // 附属数据对象是文件描述符
            cmsg.cm.cmsg_len = CMSG_LEN(sizeof(int32_t));

            // 文件描述符
            *(int32_t *) CMSG_DATA(&cmsg.cm) = open.fd();
        } else {
            msg.msg_control = NULL;
            msg.msg_controllen = 0;
        }
    }
    
    // 套接口地址，msg_name指向要发送或是接收信息的套接口地址，仅当是数据包UDP是才需要
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    
    // 实际的数据缓冲区，I/O向量引用。当要同步文件描述符，iov_base 至少一字节
    struct iovec iov[1];
    iov[0].iov_base = reinterpret_cast<char*>(buf);
    iov[0].iov_len = len;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_flags = 0;

    *size = sendmsg(mChannel[0], &msg, 0);
    if (*size >= 0 && (*size - len != 0)) {
        LOG_ERROR(soft::GetLogPath(), "%s : %s", "wChannelSocket::SendBytes, sendmsg failed", logging::NumberToString(*size).c_str());
    } else if (*size == -1) {
        if (errno == EINTR || errno == EAGAIN) {
            //LOG_ERROR(soft::GetLogPath(), "%s : %s", "wChannelSocket::SendBytes, sendmsg failed", error::Strerror(errno).c_str());
        } else {
            mStatus = wStatus::IOError("wChannelSocket::SendBytes, sendmsg failed", error::Strerror(errno));
        }
    }
    return mStatus;
}

const wStatus& wChannelSocket::RecvBytes(char buf[], size_t len, ssize_t *size) {
    mRecvTm = soft::TimeUsec();
    // msghdr.msg_control 缓冲区必须与 cmsghdr 结构对齐
    union {
        struct cmsghdr  cm;
        char space[CMSG_SPACE(sizeof(int32_t))];
    } cmsg;

    // 实际的数据缓冲区，I/O向量引用。当要同步文件描述符，iov_base 至少一字节
    struct iovec iov[1];
    iov[0].iov_base = reinterpret_cast<char*>(buf);
    iov[0].iov_len = len;

    // 附属信息，一般为同步进程间文件描述符
    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = reinterpret_cast<caddr_t>(&cmsg);
    msg.msg_controllen = sizeof(cmsg);

    *size = recvmsg(mChannel[1], &msg, 0);
    if (*size == 0) {
        mStatus = wStatus::IOError("wChannelSocket::RecvBytes, client was closed", "");
    } else if (*size == -1) {
        if (errno == EINTR || errno == EAGAIN) {
            //LOG_ERROR(soft::GetLogPath(), "%s : %s", "wChannelSocket::RecvBytes, recvmsg failed", error::Strerror(errno).c_str());
        } else {
            mStatus = wStatus::IOError("wChannelSocket::RecvBytes, recvmsg failed", error::Strerror(errno));
        }
    } else {
        if (msg.msg_flags & (MSG_TRUNC|MSG_CTRUNC)) {
        	LOG_DEBUG(soft::GetLogPath(), "%s : %s", "wChannelSocket::RecvBytes, recvmsg() truncated data", "");
        }

        // 数据协议
        uint8_t sp = static_cast<uint8_t>(coding::DecodeFixed8(buf + sizeof(uint32_t)));
        if (sp == kMpProtobuf) {
            //...
        } else {
            struct wCommand *cmd = reinterpret_cast<struct wCommand*>(buf + sizeof(uint32_t) + sizeof(uint8_t));
            if (cmd->GetId() == CmdId(CMD_CHANNEL_REQ, CHANNEL_REQ_OPEN)) {
                if (cmsg.cm.cmsg_len < static_cast<socklen_t>(CMSG_LEN(sizeof(int32_t)))) {
                    mStatus = wStatus::IOError("wChannelSocket::RecvBytes, recvmsg failed", "returned too small ancillary data");
                } else if (cmsg.cm.cmsg_level != SOL_SOCKET || cmsg.cm.cmsg_type != SCM_RIGHTS) {
                    mStatus = wStatus::IOError("wChannelSocket::RecvBytes, recvmsg failed", "returned invalid ancillary data");
                } else {
                    wChannelReqOpen_t open;
                    open.ParseFromArray(buf + sizeof(uint32_t) + sizeof(uint8_t), *size - sizeof(uint32_t) - sizeof(uint8_t));
                    int32_t fd = *(int32_t *) CMSG_DATA(&cmsg.cm);
                    open.set_fd(fd);
                    wTask::Assertbuf(buf, reinterpret_cast<char*>(&open), sizeof(open));
                }
            }
        }
    }
    return mStatus;
}

}   // namespace hnet
