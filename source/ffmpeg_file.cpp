
#include <iostream>
#include <fstream>
#include "ffmpeg_file.h"
extern "C" {
	#include <libswscale/swscale.h>
} 

FFMPEG_FILE::FFMPEG_FILE(const char* src) : recv_packet_MTX(nullptr), send_packet_video_MTX(nullptr), send_packet_audio_MTX(nullptr), recv_packet_CV(nullptr), send_packet_video_CV(nullptr), send_packet_audio_CV(nullptr), recv_packet_T(nullptr), send_packet_video_T(nullptr), send_packet_audio_T(nullptr), recv_packet_blk(false), send_packet_video_blk(false), send_packet_audio_blk(false), recv_frame(nullptr), recv_frame_video(nullptr), recv_frame_audio(nullptr) {

	state = FF_DEFAULT;
	format_context = NULL;
	video_codec_context = NULL;
	audio_codec_context = NULL;

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
	bool audio_codec_setup = false;
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

			video_codec_context = avcodec_alloc_context3(video_codec);
			if (video_codec_context == NULL) {
				printf("avcodec_alloc_context3 failed.\n");
				continue;
			}

			// -- Set decoder context parameters

			if (avcodec_parameters_to_context(video_codec_context, format_context->streams[i]->codecpar) < 0) {
				printf("avcodec_parameters_to_context failed.\n");
				continue;
			}

			// -- Initialize codec context	

			if (avcodec_open2(video_codec_context, video_codec, NULL) < 0) {
				printf("avcodec_open2 failed.\n");
				continue;
			}

			printf("Video codec width: %d.\n", video_codec_context->width);
			printf("Video codec height: %d.\n", video_codec_context->height);
			
			video_codec_setup = true;
	
		}
		else if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {	
			printf("Stream nr. %d type audio.\n", i);
            audio_stream_index = i;
            // -- Audio stream

            // -- Check for audio codec

            audio_codec = avcodec_find_decoder(format_context->streams[i]->codecpar->codec_id);
			if (audio_codec == NULL) {
				printf("avcodec_find_decoder failed.\n");
				continue;
            }

			// -- Allocate codec context

			audio_codec_context = avcodec_alloc_context3(audio_codec);
			if (audio_codec_context == NULL) {
				printf("avcodec_alloc_context3 failed.\n");
				continue;
			}		

			// -- Set decoder context parameters

			if (avcodec_parameters_to_context(audio_codec_context, format_context->streams[i]->codecpar) < 0) {
				printf("avcodec_parameters_to_context failed.\n");
				continue;
			}

			// -- Initialize codec context	

			if (avcodec_open2(audio_codec_context, audio_codec, NULL) < 0) {
				printf("avcodec_open2 failed.\n");
				continue;
			}
	
			audio_codec_setup = true;

		}
		else if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {	
			printf("Stream nr. %d type subtitle.\n", i);
		}
		else if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_UNKNOWN) {	
			printf("Stream nr. %d type unknown.\n", i);
		}
	}

	if (!video_codec_setup) return;
	if (!audio_codec_setup) return;
	
	const char* str = nullptr;
	str = av_get_pix_fmt_name(video_codec_context->pix_fmt);
	printf("Decoder format: %s.\n", str);
	
	str = av_get_pix_fmt_name(audio_codec_context->pix_fmt);
	printf("Decoder format: %s.\n", str);

	str = avcodec_get_name(audio_codec_context->codec_id);
	printf("Decoder name: %s.\n", str);

	AVRational* frame_rate_av_r = &format_context->streams[video_stream_index]->r_frame_rate;
	float frame_rate = static_cast<float>(frame_rate_av_r->num) / static_cast<float>(frame_rate_av_r->den);
	printf("Video stream frame rate: %f.\n", frame_rate);
	video_fps = frame_rate;

	recv_frame = av_frame_alloc();
	if (recv_frame == NULL) {
		printf("Failed to allocate receive frame.\n");
		return;
	}

	recv_frame_video = av_frame_alloc();
	if (recv_frame_video == NULL) {
		printf("Failed to allocate receive frame.\n");
		return;
	}

	recv_frame_audio = av_frame_alloc();
	if (recv_frame_audio == NULL) {
		printf("Failed to allocate receive frame.\n");
		return;
	}

	state = FF_ACTIVE;
	return;

}

FFMPEG_FILE::~FFMPEG_FILE() {
	
	printf("video_queue.size() = %d.\n", video_queue.size());
	printf("audio_queue.size() = %d.\n", audio_queue.size());

	if (video_codec_context != NULL) avcodec_free_context(&video_codec_context);
	if (audio_codec_context != NULL) avcodec_free_context(&audio_codec_context);
	if (format_context != NULL) avformat_close_input(&format_context);
	if (recv_frame != NULL) av_frame_free(&recv_frame);
	if (recv_frame_video != NULL) av_frame_free(&recv_frame_video);
	if (recv_frame_audio != NULL) av_frame_free(&recv_frame_audio);

	if (recv_packet_MTX != nullptr) delete recv_packet_MTX;
	if (send_packet_video_MTX != nullptr) delete send_packet_video_MTX;
	if (send_packet_audio_MTX != nullptr) delete send_packet_audio_MTX;
	if (recv_packet_CV != nullptr) delete recv_packet_CV;
	if (send_packet_video_CV != nullptr) delete send_packet_video_CV;
	if (send_packet_audio_CV != nullptr) delete send_packet_audio_CV;

	if (recv_packet_T != nullptr) delete recv_packet_T;
	if (send_packet_video_T != nullptr) delete send_packet_video_T;
	if (send_packet_audio_T != nullptr) delete send_packet_audio_T;
 
	printf("Destructing FFMPEG_FILE.\n");

	return;

}
/*
 * Caller of Read() has to av_frame_free() the returned AVFrame*
 */
AVFrame* FFMPEG_FILE::ReadVideo() {

	if (state != FF_ACTIVE) return nullptr;	

	int ret;

	// -- Read packets

	while (1) {
	
        // -- Check for available frames
        // avcodec_receive_frame unrefs recv_frame

        ret = avcodec_receive_frame(video_codec_context, recv_frame);
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
			
			if (avcodec_send_packet(video_codec_context, &recv_packet) != 0) {
				printf("avcodec_send_packet failed.\n");
				return nullptr;
			}

        }

	}

}

AVFrame* FFMPEG_FILE::ReadAudio() {

	if (state != FF_ACTIVE) return nullptr;	

	int ret;

	// -- Read packets

	while (1) {
	
        // -- Check for available frames
        // avcodec_receive_frame unrefs recv_frame

        ret = avcodec_receive_frame(audio_codec_context, recv_frame);
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

		if (recv_packet.stream_index == audio_stream_index) {
			
			// -- Send packet
			
			if (avcodec_send_packet(audio_codec_context, &recv_packet) != 0) {
				printf("avcodec_send_packet failed.\n");
				return nullptr;
			}

        }

	}

}

void FFMPEG_FILE::AsyncDecode() {

	// -- Initialize mutex and condition variables

	recv_packet_MTX = new std::mutex();
	send_packet_video_MTX = new std::mutex();
	send_packet_audio_MTX = new std::mutex();
	recv_packet_CV = new std::condition_variable();
	send_packet_video_CV = new std::condition_variable();
	send_packet_audio_CV = new std::condition_variable();

	// -- Set internal state to FF_DECODE

	state = FF_DECODE;

	// -- Start async threads

	recv_packet_T = new std::thread(&FFMPEG_FILE::AsyncRecvPacket_T, this);
	send_packet_video_T = new std::thread(&FFMPEG_FILE::AsyncSendPacketVideo_T, this);
	send_packet_audio_T = new std::thread(&FFMPEG_FILE::AsyncSendPacketAudio_T, this);
	
	return;

}

void FFMPEG_FILE::StopAsyncDecode() {

	state = FF_ACTIVE;

	printf("StopAsyncDecode().\n");

	// -- Wake up threads

	{
		std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
		printf("Unlocking AsyncRecvPacket_T.\n");
		recv_packet_blk = false;
		recv_packet_CV->notify_all();
	}
	printf("FOO.\n");
	{
		std::unique_lock<std::mutex> u_lock(*send_packet_video_MTX);
		printf("Unlocking AsyncSendPacketVideo_T.\n");
		send_packet_video_blk = false;
		send_packet_video_CV->notify_all();
	}
	printf("BAR.\n");
	{
		std::unique_lock<std::mutex> u_lock(*send_packet_audio_MTX);
		printf("Unlocking AsyncSendPacketAudio_T.\n");
		send_packet_audio_blk = false;
		send_packet_audio_CV->notify_all();
	}

	// -- Join threads

	printf("Joining threads.\n");

	if (recv_packet_T != nullptr) recv_packet_T->join();
	if (send_packet_video_T != nullptr) send_packet_video_T->join();
	if (send_packet_audio_T != nullptr) send_packet_audio_T->join();

	printf("Joined threads.\n");

	return;

}

void FFMPEG_FILE::AsyncRecvPacket_T() {

	printf("AsyncRecvPacket_T started.\n");
	AVPacket async_recv_packet;

	while (av_read_frame(format_context, &async_recv_packet) == 0 && state == FF_DECODE) {
		// printf("av_read_frame() succeeded.\n");
		if (async_recv_packet.stream_index == video_stream_index) {
			// printf("video_queue.push().\n");
			{
				std::lock_guard<std::mutex> lg(*recv_packet_MTX);
				video_queue.push(async_recv_packet);
			}
		}
		if (async_recv_packet.stream_index == audio_stream_index) {
			// printf("audio_queue.push().\n");
			{
				std::lock_guard<std::mutex> lg(*recv_packet_MTX);
				audio_queue.push(async_recv_packet);
			}

		}

		if (video_queue.size() > 100 && audio_queue.size() > 100) {

			std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
			printf("Locking AsyncRecvPacket_T.\n");
			recv_packet_blk = true;
			recv_packet_CV->wait(u_lock, [this]{ return !recv_packet_blk; });

		}

	}
	
	printf("AsyncRecvPacket_T ending.\n");

	return;

}

void FFMPEG_FILE::AsyncSendPacketVideo_T() {

	printf("AsyncSendPacketVideo_T started.\n");
	int ret;

	while (state == FF_DECODE) {

		{
			std::lock_guard<std::mutex> lg(*recv_packet_MTX);
			std::lock_guard<std::mutex> lg_2(*send_packet_video_MTX);
			if (video_queue.size() == 0) continue;
			ret = avcodec_send_packet(video_codec_context, &video_queue.front());
		}

		if (ret == 0) {
			printf("Sending video packet.\n");
			av_packet_unref(&video_queue.front());
			video_queue.pop();
		}
		else if (ret == AVERROR(EAGAIN)) {

			std::unique_lock<std::mutex> u_lock(*send_packet_video_MTX);
			printf("Locking AsyncSendPacketVideo_T.\n");
			send_packet_video_blk = true;
			send_packet_video_CV->wait(u_lock, [this]{ return !send_packet_video_blk; });

		}
		else {
			printf("AsyncSendPacketVideo_T ERROR.\n");
		}

		if (video_queue.size() < 50) {
			
			std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
			printf("Unlocking AsyncRecvPacket_T.\n");
			recv_packet_blk = false;
			recv_packet_CV->notify_all();

		}

	}

	printf("AsyncSendPacketVideo_T ending.\n");

	return;

}

void FFMPEG_FILE::AsyncSendPacketAudio_T() {

	printf("AsyncSendPacketAudio_T started.\n");
	int ret;

	while (state == FF_DECODE) {

		{
			std::lock_guard<std::mutex> lg(*recv_packet_MTX);
			std::lock_guard<std::mutex> lg_2(*send_packet_audio_MTX);
			if (audio_queue.size() == 0) continue;
			ret = avcodec_send_packet(audio_codec_context, &audio_queue.front());
		}

		if (ret == 0) {
			// printf("Sending audio packet.\n");
			av_packet_unref(&audio_queue.front());
			audio_queue.pop();
		}
		else if (ret == AVERROR(EAGAIN)) {

			std::unique_lock<std::mutex> u_lock(*send_packet_audio_MTX);
			printf("Locking AsyncSendPacketAudio_T.\n");
			send_packet_audio_blk = true;
			send_packet_audio_CV->wait(u_lock, [this]{ return !send_packet_audio_blk; });

		}
		else {
			printf("AsyncSendPacketAudio_T ERROR.\n");
		}

		if (audio_queue.size() < 50) {
			
			std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
			printf("Unlocking AsyncRecvPacket_T.\n");
			recv_packet_blk = false;
			recv_packet_CV->notify_all();

		}

	}

	printf("AsyncSendPacketAudio_T ending.\n");

	return;

}

AVFrame* FFMPEG_FILE::AsyncReadVideo() {

	printf("AsyncReadVideo().\n");
	int ret;

	while (1) {

		{
			std::lock_guard<std::mutex> lg(*send_packet_video_MTX);
			ret = avcodec_receive_frame(video_codec_context, recv_frame_video);
		}
        if (ret != 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                std::unique_lock<std::mutex> u_lock(*send_packet_video_MTX);
                printf("Frame not yet available.\n");
                send_packet_video_blk = false;
                send_packet_video_CV->notify_all();
            }
            else {
                printf("avcodec_receive_frame failed.\n");
                printf("[ERROR] AsyncReadVideo().\n");
                return nullptr;
            }
        }
        else {
			break;
        }

	}

	printf("Frame received.\n");
        
    // -- Allocate new frame and move recv_frame
    // recv_frame is ref-counted
    // have a new frame reference the same data and return it

    AVFrame* frame = av_frame_alloc();
    if (frame == NULL) {
        printf("Failed to allocate receive frame.\n");
        printf("[ERROR] AsyncReadVideo().\n");
        return nullptr;
    }

	av_frame_ref(frame, recv_frame_video);
	
	// TODO: If CTRL+C is hit then the mutex remains locked.
	// StopAsyncDecode() hangs.
	{
		std::unique_lock<std::mutex> u_lock(*send_packet_video_MTX);
		printf("Unlocking AsyncSendPacketVideo_T.\n");
		send_packet_video_blk = false;
		send_packet_video_CV->notify_all();
	}

    return frame;

}

AVFrame* FFMPEG_FILE::AsyncReadAudio() {

	printf("AsyncReadAudio().\n");
	int ret;

	while (1) {

		{
			std::lock_guard<std::mutex> lg(*send_packet_audio_MTX);
			ret = avcodec_receive_frame(audio_codec_context, recv_frame_audio);
		}
        if (ret != 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                std::unique_lock<std::mutex> u_lock(*send_packet_audio_MTX);
                // printf("Frame not yet available.\n");
                send_packet_audio_blk = false;
                send_packet_audio_CV->notify_all();
            }
            else {
                printf("avcodec_receive_frame failed.\n");
                printf("[ERROR] AsyncReadAudio().\n");
                return nullptr;
            }
        }
        else {
			break;
        }

	}

	printf("Frame received.\n");
        
    // -- Allocate new frame and move recv_frame
    // recv_frame is ref-counted
    // have a new frame reference the same data and return it

    AVFrame* frame = av_frame_alloc();
    if (frame == NULL) {
        printf("Failed to allocate receive frame.\n");
        printf("[ERROR] AsyncReadAudio().\n");
        return nullptr;
    }

	av_frame_ref(frame, recv_frame_audio);

	{
		std::unique_lock<std::mutex> u_lock(*send_packet_audio_MTX);
		printf("Unlocking AsyncSendPacketAudio_T.\n");
		send_packet_audio_blk = false;
		send_packet_audio_CV->notify_all();
	}
	
    return frame;

}
