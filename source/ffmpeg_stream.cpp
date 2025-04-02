
#include "ffmpeg_stream.h"

FFMPEG_STREAM::FFMPEG_STREAM(AVStream* stream) : stream(stream), state(FF_DEFAULT), codec(nullptr), codec_ctx(nullptr), recv_frame(nullptr) {

	// -- Check for codec

	codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (codec == nullptr) {
		printf("avcodec_find_decoder failed.\n");
		return;
	}

	OpenDecoder();

	const char* str = nullptr;
	str = av_get_pix_fmt_name(codec_ctx->pix_fmt);
	printf("Decoder format: %s.\n", str);

	str = avcodec_get_name(codec_ctx->codec_id);
	printf("Decoder name: %s.\n", str);

	printf("Sample format: %s.\n", av_get_sample_fmt_name(codec_ctx->sample_fmt));
	printf("Sample rate: %d.\n", codec_ctx->sample_rate);
	printf("Nb. of channels: %d.\n", codec_ctx->ch_layout.nb_channels);

	recv_frame = av_frame_alloc();
	if (recv_frame == nullptr) {
		printf("Failed to allocate receive frame.\n");
		return;
	}

	AVRational* time_base_av_r = &stream->time_base;
	time_base = static_cast<float>(time_base_av_r->num) / static_cast<float>(time_base_av_r->den);

	state = FF_ACTIVE;
	return;

}

FFMPEG_STREAM::~FFMPEG_STREAM() {

	if (codec_ctx != nullptr) avcodec_free_context(&codec_ctx);
	if (recv_frame != nullptr) av_frame_free(&recv_frame);

	return;

}

void FFMPEG_STREAM::OpenDecoder() {

	// -- Allocate codec context

	codec_ctx = avcodec_alloc_context3(codec);
	if (codec_ctx == nullptr) {
		printf("avcodec_alloc_context3 failed.\n");
		return;
	}

	// -- Set decoder context parameters

	if (avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0) {
		printf("avcodec_parameters_to_context failed.\n");
		return;
	}

	// -- Initialize codec context

	if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
		printf("avcodec_open2 failed.\n");
		return;
	}

	return;

}

void FFMPEG_STREAM::CloseDecoder() {

	if (codec_ctx != nullptr) avcodec_free_context(&codec_ctx);
	codec_ctx = nullptr;

	return;

}

int FFMPEG_STREAM::GetSampleRate() {
	return codec_ctx->sample_rate;
}

int FFMPEG_STREAM::GetChannelNb() {
	return codec_ctx->ch_layout.nb_channels;
}

AVSampleFormat FFMPEG_STREAM::GetSampleFormat() {
	return codec_ctx->sample_fmt;
}

FFMPEG_STATE FFMPEG_STREAM::GetState() {
	return state;
}

void FFMPEG_STREAM::PushQueue(AVPacket* packet) {
	queue.push(*packet);
	return;
}

int FFMPEG_STREAM::GetQueueSize() {
	return queue.size();
}

AVCodecContext* FFMPEG_STREAM::GetCodecContext() {
	return codec_ctx;
}

void FFMPEG_STREAM::AsyncDecode(std::shared_ptr<std::mutex> recv_packet_MTX, std::shared_ptr<std::condition_variable> recv_packet_CV, bool* recv_packet_BLK) {

	// -- Set FFMPEG_FILE mutex and condition variable

	this->recv_packet_MTX = recv_packet_MTX;
	this->recv_packet_CV = recv_packet_CV;
	this->recv_packet_BLK = recv_packet_BLK;

	// -- Allocate FFMPEG_STREAM mutex and condition variable

	send_packet_MTX = new std::mutex();
	send_packet_CV = new std::condition_variable();

	// -- Allocate FFMPEG_STREAM flush MTX and CV

	flush_MTX = new std::mutex();
	flush_CV = new std::condition_variable();

	// -- Set state to FF_FLUSH

	state = FF_FLUSH;

	// -- Start async thread

	send_packet_T = new std::thread(&FFMPEG_STREAM::AsyncSendPacket_T, this);

	return;

}

void FFMPEG_STREAM::StopAsyncDecode() {

	state = FF_ACTIVE;

	// -- Wake up async threads

	{
		std::unique_lock<std::mutex> u_lock(*send_packet_MTX);
		// printf("Unlocking AsyncSendPacket_T.\n");
		send_packet_blk = false;
		send_packet_CV->notify_all();
	}

	// -- Join async threads

	if (send_packet_T != nullptr) send_packet_T->join();

	return;

}

void FFMPEG_STREAM::AsyncSendPacket_T() {

	printf("AsyncSendPacket_T started.\n");
	int ret;

	while (state == FF_DECODE || state == FF_FLUSH) {

		if (state == FF_FLUSH) {
			{
				std::unique_lock<std::mutex> u_lock(*flush_MTX);
				// -- Notify FlushDecoder()
				flush_BLK = false;
				flush_CV->notify_all();
			}
			{
				std::unique_lock<std::mutex> u_lock(*send_packet_MTX);
				// -- Suspend thread
				send_packet_blk = true;
				send_packet_CV->wait(u_lock, [this] { return !send_packet_blk; });
			}
		}

		// -- If queue is empty notify recv_packet()
		if (queue.size() == 0) {
			std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
			*recv_packet_BLK = false;
			recv_packet_CV->notify_all();
			continue;
		}

		// -- Get packet from queue and send to decoder
		{
			std::lock_guard<std::mutex> lg(*recv_packet_MTX);
			std::lock_guard<std::mutex> lg_2(*send_packet_MTX);
			ret = avcodec_send_packet(codec_ctx, &queue.front());
		}

		if (ret == 0) {
			// printf("Sending packet.\n");
			av_packet_unref(&queue.front());
			queue.pop();
		}
		else if (ret == AVERROR(EAGAIN)) {

			std::unique_lock<std::mutex> u_lock(*send_packet_MTX);
			// printf("Locking AsyncSendPacket_T.\n");
			send_packet_blk = true;
			send_packet_CV->wait(u_lock, [this] { return !send_packet_blk; });

		}
		else {
			printf("AsyncSendPacket_T ERROR.\n");
		}

		if (queue.size() < 50) {

			std::unique_lock<std::mutex> u_lock(*recv_packet_MTX);
			// printf("Unlocking AsyncRecvPacket_T.\n");
			*recv_packet_BLK = false;
			recv_packet_CV->notify_all();

		}

	}

	printf("AsyncSendPacket_T ending.\n");

	return;

}

AVFrame* FFMPEG_STREAM::AsyncRead() {

	// printf("AsyncRead().\n");
	int ret;

	while (1) {

		if (state == FF_FLUSH) {
			// If decoder is being flushed, don't read
			// printf("Decoder flushed.\n");
			return nullptr;
		}

		{
			std::lock_guard<std::mutex> lg(*send_packet_MTX);
			std::lock_guard<std::mutex> lg_2(*flush_MTX);
			ret = avcodec_receive_frame(codec_ctx, recv_frame);
		}
		if (ret != 0) {
			if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
				std::unique_lock<std::mutex> u_lock(*send_packet_MTX);
				// printf("Frame not yet available.\n");
				send_packet_blk = false;
				send_packet_CV->notify_all();
			}
			else {
				printf("avcodec_receive_frame failed.\n");
				printf("[ERROR] AsyncRead().\n");
				return nullptr;
			}
		}
		else {
			break;
		}

	}

	// printf("Frame received.\n");

	AVFrame* frame = av_frame_alloc();
	if (frame == NULL) {
		printf("Failed to allocate receive frame.\n");
		printf("[ERROR] AsyncRead().\n");
		return nullptr;
	}

	av_frame_ref(frame, recv_frame);

	{
		std::unique_lock<std::mutex> u_lock(*send_packet_MTX);
		// printf("Unlocking AsyncSendPacket_T.\n");
		send_packet_blk = false;
		send_packet_CV->notify_all();
	}

	return frame;

}


void FFMPEG_STREAM::FlushDecoder() {

	printf("Flushing decoder.\n");

	// -- Aquire flush MTX
	{
		std::unique_lock<std::mutex> u_lock(*flush_MTX);
		// -- Set state to flush
		state = FF_FLUSH;
		{
			std::unique_lock<std::mutex> u_lock2(*send_packet_MTX);
			send_packet_blk = false;
			send_packet_CV->notify_all();
		}
		// -- Wait for SendPacket() to acknowledge
		flush_BLK = true;
		flush_CV->wait(u_lock, [this]{ return !flush_BLK; });
	}

	// -- Flush decoder

	// -- Aquire flush MTX
	{
		std::lock_guard<std::mutex> lg(*flush_MTX);
		CloseDecoder();
		OpenDecoder();
	}

	// -- Clean queue
	queue = {};

	return;

}

void FFMPEG_STREAM::StartDecoder() {

	state = FF_DECODE;

	// -- Notify send_packet
	{
		std::unique_lock<std::mutex> u_lock(*send_packet_MTX);
		send_packet_blk = false;
		send_packet_CV->notify_all();
	}

	return;

}

void FFMPEG_STREAM::Seek(int sec) {

	return;

}
