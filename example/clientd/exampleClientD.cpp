
/**
 * Copyright (C) Anny Wang.
 * Copyright (C) Hupu, Inc.
 */

#include "wCore.h"
#include "wStatus.h"
#include "wMisc.h"
#include "wTcpTask.h"
#include "wMultiClient.h"

#ifdef _USE_PROTOBUF_
#include "example.pb.h"
#else
#include "exampleCmd.h"
#endif

using namespace hnet;

class ExampleTask : public wTcpTask {
public:
	ExampleTask(wSocket *socket, int32_t type) : wTcpTask(socket, type) {
#ifdef _USE_PROTOBUF_
		On("example.ExampleEchoRes", &ExampleTask::ExampleEchoRes, this);
#else
		On(example::CMD_EXAMPLE_RES, example::EXAMPLE_RES_ECHO, &ExampleTask::ExampleEchoRes, this);
#endif
	}
    virtual const wStatus& ReConnect();
	
	int ExampleEchoRes(struct Request_t *request);
};

const wStatus& ExampleTask::ReConnect() {

std::cout << "ReConnect" << std::endl;

// 重连事件
#ifdef _USE_PROTOBUF_
	example::ExampleEchoReq req;
#else
	example::ExampleReqEcho_t req;
#endif
	req.set_cmd("hello hnet~");

#ifdef _USE_PROTOBUF_
	AsyncSend(&req);
#else
	AsyncSend(reinterpret_cast<char*>(&req), sizeof(req));
#endif
	return mStatus.Clear();
}

int ExampleTask::ExampleEchoRes(struct Request_t *request) {
#ifdef _USE_PROTOBUF_
	example::ExampleEchoRes res;
#else
	example::ExampleResEcho_t res;
#endif
	res.ParseFromArray(request->mBuf, request->mLen);
	std::cout << res.cmd() << "|" << res.ret() << std::endl;

// 循环请求
#ifdef _USE_PROTOBUF_
	example::ExampleEchoReq req;
#else
	example::ExampleReqEcho_t req;
#endif
	req.set_cmd("hello hnet~");

#ifdef _USE_PROTOBUF_
	AsyncSend(&req);
#else
	AsyncSend(reinterpret_cast<char*>(&req), sizeof(req));
#endif
	return 0;
}

class ExampleClient : public wMultiClient {
public:
	ExampleClient(wConfig* config, wServer* server = NULL) : wMultiClient(config, server) { }

	virtual const wStatus& NewTcpTask(wSocket* sock, wTask** ptr, int type = 0) {
	    SAFE_NEW(ExampleTask(sock, type), *ptr);
	    if (*ptr == NULL) {
	        return mStatus = wStatus::IOError("ExampleClient::NewTcpTask", "new failed");
	    }
	    return mStatus;
	}

	virtual const wStatus& PrepareRun() {
	    std::string host;
	    int16_t port = 0;

	    wConfig* config = Config<wConfig*>();
	    if (config == NULL || !config->GetConf("host", &host) || !config->GetConf("port", &port)) {
	    	return mStatus = wStatus::IOError("ExampleClient::PrepareRun failed", "Config() is null or host|port is illegal");
	    }

	    // 连接服务器
		int type = 1;	// 该类服务器key，每个key可挂载连接多个服务器
		if (!AddConnect(type, host, port).Ok()) {
			std::cout << "connect error:" << mStatus.ToString() << std::endl;
		}
		return mStatus;
	}
};

int main(int argc, const char *argv[]) {

	// 创建配置对象
	wConfig* config;
	SAFE_NEW(wConfig, config);
	if (config == NULL) {
		return -1;
	}
	wStatus s;

	// 解析命令行
	s = config->GetOption(argc, argv);
	if (!s.Ok()) {
		std::cout << "get configure:" << s.ToString() << std::endl;
		return -1;
	}
	if (misc::SetBinPath() == -1) {
		std::cout << "set bin path failed" << std::endl;
	}

	// 版本输出 && 守护进程创建
	bool version, daemon;
	if (config->GetConf("version", &version) && version == true) {
		std::cout << soft::SetSoftName("example client daemon -") << soft::GetSoftVer() << std::endl;
		return -1;
	} else if (config->GetConf("daemon", &daemon) && daemon == true) {
		std::string lock_path;
		config->GetConf("lock_path", &lock_path);
		if (!misc::InitDaemon(lock_path).Ok()) {
			std::cout << "create daemon failed" << std::endl;
			return -1;
		}
	}

	// 创建客户端
    ExampleClient* client;
	SAFE_NEW(ExampleClient(config), client);
	if (client == NULL) {
		return -1;
	}

	// 准备客户端
	s = client->PrepareStart();
	if (s.Ok()) {
		/** 阻塞服务方式运行*/
		//s = client->Start();
		//std::cout << "start end:" << s.ToString() << std::endl;

		/** 守护线程方式运行*/
		std::cout << "thread start" << std::endl;
		client->StartThread();

		// 广播发送example::ExampleEchoReq请求到所有连接的服务器
#ifdef _USE_PROTOBUF_
		example::ExampleEchoReq req;
#else
		example::ExampleReqEcho_t req;
#endif
		req.set_cmd("hello hnet~");
		
#ifdef _USE_PROTOBUF_
		client->Broadcast(&req, 1);
#else
		client->Broadcast(reinterpret_cast<char*>(&req), sizeof(req), 1);
#endif
		// 等待连接结束
		client->JoinThread();
		std::cout << "thread end" << std::endl;
	} else {
		std::cout << "prepare error:" << s.ToString() << std::endl;
		return -1;
	}

	return 0;
}
