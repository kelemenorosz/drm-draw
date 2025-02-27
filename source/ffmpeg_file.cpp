
#include <iostream>
#include <fstream>
#include "ffmpeg_file.h"
extern "C" {
	#include <libswscale/swscale.h>
} 

FFMPEG_FILE::FFMPEG_FILE(const char* src) {

	state = FF_DEFAULT;
	format_context = NULL;
	codec_context = NULL;
	recv_frame = NULL;

	// -- Create context on file

	if (avformat_open_input(&format_context, src, NULL, NULL) != 0) {	
		std::cout << "avformat_open_input error." << std::endl;	
		return;
	}
	
	// -- Get stream info
	// This isn't necessary for some files. But just in case.

	if (avformat_find_stream_info(format_context, NULL) < 0) {
		std::cout << "avformat_close_input error." << std::endl;
		return;
	}

	// -- Check for video streams

	printf("Number of streams: %d.\n", format_context->nb_streams);
	
	bool video_codec_setup = false;	
	for (int i = 0; i < format_context->nb_streams; ++i) {
	
		if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			printf("Stream nr. %d type video.\n", i);
			video_stream_index = i;
			// -- Video stream
	
			// -- Check for video codec

			video_codec = avcodec_find_decoder(format_context->streams[i]->codecpar->codec_id);
			if (video_codec == NULL) {
				printf("avcodec_find_decoder failed.\n");
				continue;
			}

			// -- Allocate codec context

			codec_context = avcodec_alloc_context3(video_codec);
			if (codec_context == NULL) {
				printf("avcodec_alloc_context3 failed.\n");
				continue;
			}

			// -- Set decoder context parameters

			if (avcodec_parameters_to_context(codec_context, format_context->streams[i]->codecpar) < 0) {
				printf("avcodec_parameters_to_context failed.\n");
				continue;
			}

			// -- Initialize codec context	

			if (avcodec_open2(codec_context, video_codec, NULL) < 0) {
				printf("avcodec_open2 failed.\n");
				continue;
			}

			printf("Video codec width: %d.\n", codec_context->width);
			printf("Video codec height: %d.\n", codec_context->height);
			
			video_codec_setup = true;
	
		}
		else if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {	
			printf("Stream nr. %d type audio.\n", i);
		}
		else if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {	
			printf("Stream nr. %d type subtitle.\n", i);
		}
		else if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_UNKNOWN) {	
			printf("Stream nr. %d type unknown.\n", i);
		}
	}

	if (!video_codec_setup) return;
	
	const char* str = av_get_pix_fmt_name(codec_context->pix_fmt);
	printf("Decoder format: %s.\n", str);

	recv_frame = av_frame_alloc();
	if (recv_frame == NULL) {
		printf("Failed to allocate receive frame.\n");
		return;
	}

	state = FF_ACTIVE;
	return;

}

FFMPEG_FILE::~FFMPEG_FILE() {
	
	if (codec_context != NULL) avcodec_free_context(&codec_context);
	if (format_context != NULL) avformat_close_input(&format_context);
	if (recv_frame != NULL) av_frame_free(&recv_frame);

	return;

}
/*
 * Caller of Read() has to av_frame_free() the returned AVFrame*
 */
AVFrame* FFMPEG_FILE::Read() {

	if (state != FF_ACTIVE) return nullptr;	

	int ret;

	// -- Read packets

	while (1) {
	
        // -- Check for available frames
        // avcodec_receive_frame unrefs recv_frame

        ret = avcodec_receive_frame(codec_context, recv_frame);
        if (ret != 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                printf("Frame not yet available.\n");
            }
            else {
                printf("avcodec_receive_frame failed.\n");
                return nullptr;
            }
        }
        else {
            printf("Frame received.\n");
        
            // -- Allocate new frame and move recv_frame
            // recv_frame is ref-counted
            // have a new frame reference the same data and return it

            AVFrame* frame = av_frame_alloc();
            if (frame == NULL) {
                printf("Failed to allocate receive frame.\n");
                return nullptr;
            }
        
            av_frame_ref(frame, recv_frame);           
            return frame;

        }

        // -- If no frames are available read packet

		if (av_read_frame(format_context, &recv_packet) < 0) {
			printf("av_read_frame failed.\n");
			return nullptr;
		}

		// -- Check for video stream packet

		if (recv_packet.stream_index == video_stream_index) {
			
			// -- Send packet
			
			if (avcodec_send_packet(codec_context, &recv_packet) != 0) {
				printf("avcodec_send_packet failed.\n");
				return nullptr;
			}

        }

	}

}
