// testRtspClient.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#include <sstream>
#include <iostream>
#include <stdio.h>
#include <functional>
#include <thread>


#include "../../Live555Client.h"

using namespace std;

class RollStepPrinter
{
public:
	void Step()
	{
		switch (m_Step)
		{
		case 0:
			printf("\\");
			break;
		case 1:
			printf("\b");
			printf("|");
			break;		
		case 2:
			printf("\b");
			printf("-");
			break;		
		case 3:
			printf("\b");
			break;
		case 4:
			printf("/");
			break;
		case 5:
			printf("\b");
			break;
		default:
			break;
		}
		m_Step++;
		if (m_Step > 5)
			m_Step = 0;
	}
	int m_Step = 0;
};
class DemuxLive555 : public Live555Client
{
public:
	virtual void onInitializedTrack(LiveTrack* track)
	{
		LiveTrack::media_format& fmt = track->getFormat();
		std::string SessionName = track->getSessionName();

		if (fmt.type == "video") {
			cout << "video:" << SessionName << endl;

		}
		if (fmt.type == "audio") {
			cout << "aduio:" << SessionName << endl;
		}
	}

	void Loop()
	{
		int Status = RTSP_OK;

		while (m_MyLoop)
		{
			Status = PlayRtsp(m_Uri);

			if (Status == RTSP_TIMEOUT) {
				std::cout << "timeout" << std::endl;

				if (m_ReconnectOnTimeOut) {
					std::cout << "reconnect" << std::endl;
				}
				else {
					std::cout << "quit" << std::endl;
					break;
				}
			}
			else if(Status == RTSP_EOF) {
				std::cout << "rtsp eof" << std::endl;
				if (m_ReconnectOnEof) {
					std::cout << "reconnect" << std::endl;
				}
				else {
					std::cout << "quit" << std::endl;
					break;
				}
			}
			else if (Status == RTSP_ERR) {
				std::cout << "rtsp err" << std::endl;
				break;
			}
			else if (Status == RTSP_USR_STOP) {
				std::cout << "usr stop" << std::endl;
				break;
			}
			else if (Status == RTSP_OK) {

			}
			else {
				std::cout << "weird code:" << Status << std::endl;

			}
			//避免疯狂的再次重连
			std::this_thread::sleep_for(std::chrono::seconds(m_ReconnectGap));
		}

		m_RtspStatus = Status;

		std::cout << "rtsp thread quit" << std::endl;
		m_MyLoop = false;
	}

	void Play(std::string Uri)
	{
		m_Uri = Uri;
		m_MyLoop = true;

		std::thread demuxThread(&DemuxLive555::Loop, this);
		m_demuxLoop = std::move(demuxThread);
	}

	void Stop()
	{
		if (m_MyLoop == false) return;	//已经退出

		//先设置自己的标志位
		m_MyLoop = 0;
		//停止主循环
		StopRtsp();
		//等待线程结束
		m_demuxLoop.join();
	}

	virtual void onData(LiveTrack* track,
		uint8_t* p_buffer,
		int i_size,
		int i_truncated_bytes,
		int64_t pts, int64_t dts)
	{
		if (PrintTs) {
			std::string SessionName = track->getSessionName();
			int64_t deltaPts = pts - lastPts;
			int64_t deltaDts = dts - lastDts;
			cout << SessionName << "," << i_size << ","
				<< "pts:" << pts << ",dt:" << deltaPts << ","
				<< "dts:" << dts << ",dt:" << deltaDts << endl;

			lastPts = pts;
			lastDts = dts;
		}
		else {
			m_Roll.Step();
		}

		LiveTrack::media_format& fmt = track->getFormat();

		//如果是H264就保存
		if ((DstFile.size() > 0) && (fmt.type == "video")
			&& (fmt.codec == "H264")) {
			FILE* fp = fopen(DstFile.c_str(), "ab");
			fwrite(p_buffer, i_size, 1, fp);
			fclose(fp);
		}
	}
	string DstFile;

	bool PrintTs = true;
	int m_ReconnectOnTimeOut = true;
	int m_ReconnectOnEof = false;

	int GetStatus() { return m_RtspStatus; }
private:
	int64_t lastPts = 0, lastDts = 0;
	RollStepPrinter m_Roll;
	int m_RtspStatus = RTSP_OK;
	std::string m_UsrName;
	std::string m_Password;
	std::string m_Uri;
	std::thread m_demuxLoop;

	int m_ReconnectGap = 3;				//重连间隔,秒
	volatile bool m_MyLoop = false;
};


using namespace std::chrono;
using namespace std;

struct RtspTest
{
	//测试开关
	int bTest;
	//描述,用于打印
	const char* describe;
	//测试函数
	std::function<void(void)> f;
};

int main()
{
	const char* normalRtspAddr  = "rtsp://192.168.103.45/ch0.liv";
	const char* normalRtspAddr2 = "rtsp://192.168.103.46/ch0.liv";
	const char* NoExistRtspAddr = "rtsp://192.168.103.99/ch0.liv";
	//1分钟以内的短文件用Live555做Server
	const char* RtspAddrWileEof = "rtsp://127.0.0.1/h264RtspTestSave.264";

	RtspTest NowTest[] =
	{
		{0,"测试一个不存在的地址并迅速关闭",[&] {
			DemuxLive555 demux0;

			demux0.Play(NoExistRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}

			demux0.Stop();
		} },

		{0,"测试一个不存在的地址并尝试不断连接10秒后关闭",[&] {
			DemuxLive555 demux0;

			demux0.Play(NoExistRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}

			demux0.Stop();
		}},

		{0,"测试一个存在的地址并迅速关闭",[&] {
			DemuxLive555 demux0;

			demux0.Play(normalRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}

			demux0.Stop();
		}},

		{0,"测试一个存在的地址并连接10秒后关闭",[&] {
			DemuxLive555 demux0;

			demux0.Play(normalRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}

			demux0.Stop();
		}},

		{0,"单路疯狂的播放停止",[&] {
			int testTime = 5;
			while (testTime-- > 0)
			{
				DemuxLive555 demux1;

				demux1.Play(normalRtspAddr);

				{
					std::this_thread::sleep_for(std::chrono::seconds(5));
				}

				demux1.Stop();
			}
		}},

		{0,"测试多路",[&] {
			int testTime = 5;
			while (testTime-- > 0)
			{
				DemuxLive555 demux1;
				DemuxLive555 demux2;

				demux1.Play(normalRtspAddr);
				demux2.Play(normalRtspAddr2);

				{
					std::this_thread::sleep_for(std::chrono::seconds(5));
				}

				demux1.Stop();
				demux2.Stop();
			}
		}},

		{0,"测试单路H264数据完整性",[&] {
			DemuxLive555 demux1;
			demux1.DstFile = "h264RtspTestSave.h264";
			demux1.Play(normalRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}

			demux1.Stop();
		}},

		{ 0,"测试多路H264数据完整性",[&] {
			DemuxLive555 demux1;
			demux1.PrintTs = false;
			demux1.DstFile = "h264RtspTestSave1.h264";
			demux1.Play(normalRtspAddr);

			DemuxLive555 demux2;
			demux2.PrintTs = false;
			demux2.DstFile = "h264RtspTestSave2.h264";
			demux2.Play(normalRtspAddr2);

			{
				std::this_thread::sleep_for(std::chrono::seconds(20));
			}
			demux1.Stop();
			demux2.Stop();
		} },

		{ 0,"测试RTSP处理EOF",[&] {
			DemuxLive555 demux0;
			demux0.PrintTs = false;
			demux0.Play(RtspAddrWileEof);

			{
				std::this_thread::sleep_for(std::chrono::seconds(120));
			}

			demux0.Stop();
		} },

		{ 1,"测试一个存在的地址并长时间播放",[&] {
			DemuxLive555 demux0;

			demux0.Play(normalRtspAddr);
			demux0.PrintTs = false;
			demux0.m_ReconnectOnEof = true;
			{
				std::this_thread::sleep_for(std::chrono::hours(5));
			}

			demux0.Stop();
		} },
	};

	int size = sizeof(NowTest) / sizeof(RtspTest);
	for (int i = 0; i < size; i++) {
		if (NowTest[i].bTest) {
			printf("%s\n", NowTest[i].describe);
			NowTest[i].f();
		}
	}

	printf(" 测试完成\n");

	cin.get();

	return 0;
}

