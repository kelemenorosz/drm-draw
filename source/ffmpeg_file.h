#ifndef FFMPEG_FILE_H
#define FFMPEG_FILE_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "ffmpeg_stream.h"
#include "ffmpeg_state.h"

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libavutil/avutil.h>
	#include <libavutil/pixdesc.h>
	#include <libavutil/imgutils.h>
	#include <libswscale/swscale.h>
}

class FFMPEG_FILE {

private:

	FFMPEG_STATE state;

	FFMPEG_STREAM* video_stream;
	FFMPEG_STREAM* audio_stream;

	AVFormatContext* format_ctx;
	AVPacket recv_packet;

	int video_stream_index;
	int audio_stream_index;

	float audio_time_base;

	std::thread* recv_packet_T;
	std::shared_ptr<std::mutex> recv_packet_MTX;
	std::shared_ptr<std::condition_variable> recv_packet_CV;
	bool recv_packet_BLK;

	void AsyncRecvPacket_T();

public:

	FFMPEG_FILE() = delete;
	FFMPEG_FILE(const char*);
	~FFMPEG_FILE();

	void SeekAudio(int sec);

	AVCodecContext* GetVideoCodecContext();

	void AsyncDecode();
	void StopAsyncDecode();
	AVFrame* AsyncReadVideo();
	AVFrame* AsyncReadAudio();

	float video_fps;

};

#endif /*FFMPEG_FILE_H */
