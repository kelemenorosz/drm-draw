#ifndef FFMPEG_STREAM_H
#define FFMPEG_STREAM_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "ffmpeg_state.h"

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libavutil/avutil.h>
	#include <libavutil/pixdesc.h>
	#include <libavutil/imgutils.h>
	#include <libswscale/swscale.h>
}

class FFMPEG_STREAM {

private:

	FFMPEG_STATE state;

	const AVCodec* codec;
	AVCodecContext* codec_ctx;

	AVFrame* recv_frame;

	int stream_index;

	std::queue<AVPacket> queue;

	std::shared_ptr<std::mutex> recv_packet_MTX;
	std::shared_ptr<std::condition_variable> recv_packet_CV;
	bool* recv_packet_BLK;

	std::thread* send_packet_T;

	std::mutex* send_packet_MTX;
	std::condition_variable* send_packet_CV;
	bool send_packet_BLK;

public:

	FFMPEG_STREAM() = delete;
	FFMPEG_STREAM(AVStream* stream);
	~FFMPEG_STREAM();

	FFMPEG_STATE GetState();
	int GetQueueSize();
	void AsyncDecode(std::shared_ptr<std::mutex> recv_packet_MTX, std::shared_ptr<std::condition_variable> recv_packet_CV, bool* recv_packet_BLK);
	void StopAsyncDecode();
	AVFrame* AsyncRead();
	void AsyncSendPacket_T();
	void PushQueue(AVPacket* packet);
	AVCodecContext* GetCodecContext();

	AVStream* stream;


};

#endif /* FFMPEG_STREAM_H */