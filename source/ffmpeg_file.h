#ifndef FFMPEG_FILE_H
#define FFMPEG_FILE_H

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
	};

	FFMPEG_STATE state;

	AVFormatContext* format_context;
	const AVCodec* video_codec;	
	AVPacket recv_packet;
	AVFrame* recv_frame;

	int video_stream_index;

public:

	FFMPEG_FILE() = delete;
	FFMPEG_FILE(const char*);
	~FFMPEG_FILE();
	
	AVFrame* Read();

	AVCodecContext* codec_context;

};

#endif /*FFMPEG_FILE_H */
