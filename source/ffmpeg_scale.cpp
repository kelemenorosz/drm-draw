
#include "ffmpeg_scale.h"

using namespace FFMPEG_SCALE;

AVFrame* FFMPEG_SCALE::RGB(AVFrame* src, AVCodecContext* codec, uint8_t** buf, AVPixelFormat format) {

/*
	const char* str = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));
	printf("%s\n", str);
*/

    // -- Convert image
	
	SwsContext* sws_context = NULL;

	// -- Create sws context

	sws_context = sws_getContext(codec->width, codec->height, codec->pix_fmt, codec->width, codec->height, format, SWS_BICUBIC, NULL, NULL, NULL);

	// -- Setup destination frame

	AVFrame* dst_frame = av_frame_alloc();
	if (dst_frame == NULL) {
		printf("Failed to allocate destination frame.\n");
		return nullptr;
	}

	dst_frame->width = codec->width;
	dst_frame->height = codec->height;
	int dst_buf_size = av_image_get_buffer_size(format, dst_frame->width, dst_frame->height, 1);

	printf("Destination frame buffer size: %d.\n", dst_buf_size);

	uint8_t* dst_buf = (uint8_t*)av_mallocz(dst_buf_size);
	if (av_image_fill_arrays(dst_frame->data, dst_frame->linesize, dst_buf, format, codec->width, codec->height, 1) < 0) {
		printf("av_image_fill_arrays failed.\n");
		av_freep(reinterpret_cast<void*>(dst_buf));
		av_frame_free(&dst_frame);
		return nullptr;
	}

	// -- Execute conversion

	if (sws_scale(sws_context, (unsigned char const * const *)(src->data), src->linesize, 0, codec->height, dst_frame->data, dst_frame->linesize) <= 0) {
		printf("sws_scale failed.\n");
		av_freep(reinterpret_cast<void*>(dst_buf));
		av_frame_free(&dst_frame);
		return nullptr;
	}

    *buf = dst_buf;
    return dst_frame;
       
}

