#ifndef FFMPEG_SCALE_H
#define FFMPEG_SCALE_H


extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libavutil/avutil.h>
	#include <libavutil/pixdesc.h>
	#include <libavutil/imgutils.h>
	#include <libswscale/swscale.h>
}

namespace FFMPEG_SCALE {

    AVFrame* RGB(AVFrame* src, AVCodecContext* codec, uint8_t** buf, AVPixelFormat format);

}

#endif /* FFMPEG_SCALE_H */
