
#include <iostream>
#include <fstream>
#include "ffmpeg_file.h"
extern "C" {
	#include <libswscale/swscale.h>
} 

FFMPEG_FILE::FFMPEG_FILE(const char* src) : format_ctx(nullptr), video_stream(nullptr), recv_packet_MTX(nullptr), recv_packet_CV(nullptr), recv_packet_T(nullptr), recv_packet_BLK(false), active_audio(0), frames_read_audio(0) {

	state = FF_DEFAULT;

	// -- Create context on file

	if (avformat_open_input(&format_ctx, src, NULL, NULL) != 0) {
		std::cout << "avformat_open_input error." << std::endl;
		return;
	}

	// -- Get stream info
	// This isn't necessary for some files. But just in case.

	if (avformat_find_stream_info(format_ctx, NULL) < 0) {
		std::cout << "avformat_close_input error." << std::endl;
		return;
	}

	// -- Check for streams

	printf("Number of streams: %d.\n", format_ctx->nb_streams);

	bool video_codec_setup = false;
	bool audio_codec_setup = false;

	for (int i = 0; i < format_ctx->nb_streams; ++i) {

		if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			// -- Video stream
			printf("Stream nr. %d type video.\n", i);
			video_stream_index = i;
			video_stream = new FFMPEG_STREAM(format_ctx->streams[i]);
			if (video_stream->GetState() == FF_ACTIVE) video_codec_setup = true;
		}
		else if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			// -- Audio stream
			printf("Stream nr. %d type audio.\n", i);
			// audio_stream_index = i;
			// audio_stream = new FFMPEG_STREAM(format_ctx->streams[i]);
			audio_streams.emplace_back(i, new FFMPEG_STREAM(format_ctx->streams[i]));
			if (audio_streams[audio_streams.size() - 1].second->GetState() == FF_ACTIVE) {
				audio_codec_setup = true;
				printf("Stream state: ACTIVE.\n");
			}
			else {
				printf("Stream state: NOT ACTIVE.\n");
			}
		}
		else if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
			printf("Stream nr. %d type subtitle.\n", i);
		}
		else if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_UNKNOWN) {
			printf("Stream nr. %d type unknown.\n", i);
		}
		else {
			printf("Stream nr. %d type REALLY unknown.\n", i);
		}
	}

	printf("Number of audio streams: %d.\n", audio_streams.size());

	if (!video_codec_setup) return;
	if (!audio_codec_setup) return;

	AVRational* frame_rate_av_r = &video_stream->stream->r_frame_rate;
	float frame_rate = static_cast<float>(frame_rate_av_r->num) / static_cast<float>(frame_rate_av_r->den);
	printf("Video stream frame rate: %f.\n", frame_rate);
	video_fps = frame_rate;

	// -- TODO: calculate for every stream
	// AVRational* time_base_av_r = &audio_stream->stream->time_base;
	AVRational* time_base_av_r = &audio_streams[0].second->stream->time_base;
	float time_base = static_cast<float>(time_base_av_r->num) / static_cast<float>(time_base_av_r->den);
	printf("Audio stream time base: %f.\n", time_base);
	audio_time_base = time_base;

	time_base_av_r = &video_stream->stream->time_base;
	time_base = static_cast<float>(time_base_av_r->num) / static_cast<float>(time_base_av_r->den);
	printf("Video stream time base: %f.\n", time_base);
	video_time_base = time_base;

	state = FF_ACTIVE;
	return;

}

FFMPEG_FILE::~FFMPEG_FILE() {
	
	printf("video_queue.size() = %d.\n", video_stream->GetQueueSize());
	// printf("audio_queue.size() = %d.\n", audio_stream->GetQueueSize());
	printf("audio_queue.size() = %d.\n", audio_streams[active_audio].second->GetQueueSize());
	printf("Audio frames read: %d.\n", frames_read_audio);

	if (video_stream != nullptr) delete video_stream;
	// if (audio_stream != nullptr) delete audio_stream;

	if (format_ctx != NULL) avformat_close_input(&format_ctx);

	if (recv_packet_T != nullptr) delete recv_packet_T;
 
	printf("Destructing FFMPEG_FILE.\n");

	return;

}

void FFMPEG_FILE::SeekAudio(int sec) {

	float timestamp_n = static_cast<float>(sec) / audio_time_base;
	av_seek_frame(format_ctx, audio_streams[active_audio].first, static_cast<int>(timestamp_n), 0);

	return;

}

float FFMPEG_FILE::SeekVideo(int sec) {

	printf("Seeking video.\n");

	// -- Wake up RecvPacket() and stop receiveing packets
	{
		std::unique_lock<std::mutex> u_lock(*flush_MTX);
		state = FF_FLUSH;
		{
			std::unique_lock<std::mutex> u_lock2(*recv_packet_MTX);
			recv_packet_BLK = false;
			recv_packet_CV->notify_all();
		}
		flush_BLK = true;
		flush_CV->wait(u_lock, [this]{ return !flush_BLK; });
	}

	// -- Flush decoder
	// audio_stream->FlushDecoder();
	audio_streams[active_audio].second->FlushDecoder();
	video_stream->FlushDecoder();

	int timestamp_n = static_cast<float>(sec) / video_time_base;
	av_seek_frame(format_ctx, video_stream_index, timestamp_n, 0);

	// -- Start decoder
	video_stream->StartDecoder();
	audio_streams[active_audio].second->StartDecoder();

	return (static_cast<float>(timestamp_n) * video_time_base);

}

float FFMPEG_FILE::SwitchAudio(int stream, float sec) {

	printf("Switching audio.\n");

	// -- Wake up RecvPacket() and stop receiveing packets
	{
		std::unique_lock<std::mutex> u_lock(*flush_MTX);
		state = FF_FLUSH;
		{
			std::unique_lock<std::mutex> u_lock2(*recv_packet_MTX);
			recv_packet_BLK = false;
			recv_packet_CV->notify_all();
		}
		flush_BLK = true;
		flush_CV->wait(u_lock, [this]{ return !flush_BLK; });
	}

	// -- Flush decoders
	video_stream->FlushDecoder();
	audio_streams[active_audio].second->FlushDecoder();

	// -- Seek to current frames read
	int timestamp_n = sec / video_time_base;
	printf("timestamp_n: %f.\n", timestamp_n);
	printf("sec: %f.\n", sec);
	printf("video time base: %f.\n", video_time_base);
	av_seek_frame(format_ctx, video_stream_index, timestamp_n, 0);

	active_audio = stream;

	// -- Start decoder
	video_stream->StartDecoder();
	audio_streams[active_audio].second->StartDecoder();

	return (static_cast<float>(timestamp_n) * video_time_base);

}

AVCodecContext* FFMPEG_FILE::GetVideoCodecContext() {
	return video_stream->GetCodecContext();
}

void FFMPEG_FILE::AsyncDecode() {

	// -- Initialize mutex and condition variables

	recv_packet_MTX = std::make_shared<std::mutex>();
	recv_packet_CV = std::make_shared<std::condition_variable>();

	flush_MTX = new std::mutex();
	flush_CV = new std::condition_variable();

	// -- Set internal state to FF_DECODE

	state = FF_DECODE;

	// -- Start stream async threads

	video_stream->AsyncDecode(recv_packet_MTX, recv_packet_CV, &recv_packet_BLK);
	// audio_streams[active_audio].second->AsyncDecode(recv_packet_MTX, recv_packet_CV, &recv_packet_BLK);
	for (int i = 0; i < audio_streams.size(); ++i) {
		audio_streams[i].second->AsyncDecode(recv_packet_MTX, recv_packet_CV, &recv_packet_BLK);
	}

	// -- Start async threads

	recv_packet_T = new std::thread(&FFMPEG_FILE::AsyncRecvPacket_T, this);

	video_stream->StartDecoder();
	audio_streams[active_audio].second->StartDecoder();

	return;

}

void FFMPEG_FILE::StopAsyncDecode() {

	state = FF_ACTIVE;

	// printf("StopAsyncDecode().\n");

	// -- Wake up async threads

	{
		std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
		// printf("Unlocking AsyncRecvPacket_T.\n");
		recv_packet_BLK = false;
		recv_packet_CV->notify_all();
	}

	// -- Stop stream async threads

	video_stream->StopAsyncDecode();
	audio_streams[active_audio].second->StopAsyncDecode();

	// -- Join threads

	// printf("Joining threads.\n");

	if (recv_packet_T != nullptr) recv_packet_T->join();

	// printf("Joined threads.\n");

	return;

}

void FFMPEG_FILE::AsyncRecvPacket_T() {

	// printf("AsyncRecvPacket_T started.\n");
	AVPacket async_recv_packet;

	while (state == FF_DECODE || state == FF_FLUSH) {

		if (state == FF_FLUSH) {
			{
				std::unique_lock<std::mutex> u_lock(*flush_MTX);
				flush_BLK = false;
				flush_CV->notify_all();
			}
			// -- Suspend thread
			{
				std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
				recv_packet_BLK = true;
				recv_packet_CV->wait(u_lock, [this]{ return !recv_packet_BLK; });
			}
		}

		if (av_read_frame(format_ctx, &async_recv_packet) != 0) {
			return;
		}

		// printf("av_read_frame() succeeded.\n");
		if (async_recv_packet.stream_index == video_stream_index) {
			// printf("video_queue.push().\n");
			{
				std::lock_guard<std::mutex> lg(*recv_packet_MTX);
				video_stream->PushQueue(&async_recv_packet);
				// printf("video_queue.push() succeeded.\n");
			}
		}
		if (async_recv_packet.stream_index == audio_streams[active_audio].first) {
			// printf("audio_queue.push().\n");
			{
				std::lock_guard<std::mutex> lg(*recv_packet_MTX);
				audio_streams[active_audio].second->PushQueue(&async_recv_packet);
			}

		}

		// printf("Video queue size: %d, audio_queue_size: %d.\n", video_stream->GetQueueSize(), audio_stream->GetQueueSize());

		if (video_stream->GetQueueSize() > 100 && audio_streams[active_audio].second->GetQueueSize() > 100) {

			std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
			// printf("Locking AsyncRecvPacket_T.\n");
			recv_packet_BLK = true;
			recv_packet_CV->wait(u_lock, [this]{ return !recv_packet_BLK; });

		}
	}



	// printf("AsyncRecvPacket_T ending.\n");

	return;

}

AVFrame* FFMPEG_FILE::AsyncReadVideo() {

	return video_stream->AsyncRead();

}

AVFrame* FFMPEG_FILE::AsyncReadAudio() {

	frames_read_audio++;
	return audio_streams[active_audio].second->AsyncRead();

}

int FFMPEG_FILE::GetSampleRate() {
	return audio_streams[active_audio].second->GetSampleRate();
}

int FFMPEG_FILE::GetChannelNb() {
	return audio_streams[active_audio].second->GetChannelNb();
}

AVSampleFormat FFMPEG_FILE::GetSampleFormat() {
	return audio_streams[active_audio].second->GetSampleFormat();
}

int FFMPEG_FILE::GetAudioTrackNb() {
	return audio_streams.size();
}
