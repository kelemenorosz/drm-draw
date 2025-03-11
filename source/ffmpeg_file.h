#ifndef FFMPEG_FILE_H
#define FFMPEG_FILE_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

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

	enum FFMPEG_STATE {
		FF_DEFAULT = 0,
		FF_ACTIVE,
		FF_DECODE
	};

	FFMPEG_STATE state;

	AVFormatContext* format_context;
	const AVCodec* video_codec;
	const AVCodec* audio_codec;
	AVPacket recv_packet;
	AVFrame* recv_frame;
	AVFrame* recv_frame_video;
	AVFrame* recv_frame_audio;

	int video_stream_index;
	int audio_stream_index;

	int queue_size;
	std::queue<AVPacket> video_queue;
	std::queue<AVPacket> audio_queue;

	std::thread* recv_packet_T;
	std::thread* send_packet_video_T;
	std::thread* send_packet_audio_T;

	std::mutex* recv_packet_MTX;
	std::mutex* send_packet_video_MTX;
	std::mutex* send_packet_audio_MTX;
	std::condition_variable* recv_packet_CV;
	std::condition_variable* send_packet_video_CV;
	std::condition_variable* send_packet_audio_CV;
	bool recv_packet_blk;
	bool send_packet_video_blk;
	bool send_packet_audio_blk;

	void AsyncRecvPacket_T();
	void AsyncSendPacketVideo_T();
	void AsyncSendPacketAudio_T();

public:

	FFMPEG_FILE() = delete;
	FFMPEG_FILE(const char*);
	~FFMPEG_FILE();

	AVFrame* ReadVideo();
	AVFrame* ReadAudio();

	void AsyncDecode();
	void StopAsyncDecode();
	AVFrame* AsyncReadVideo();
	AVFrame* AsyncReadAudio();

	AVCodecContext* video_codec_context;
	AVCodecContext* audio_codec_context;

	float video_fps;

};

#endif /*FFMPEG_FILE_H */
