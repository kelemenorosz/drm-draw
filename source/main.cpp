#include <iostream>
#include <cstring>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <random>
#include <alsa/asoundlib.h>
#include "dumb_buffer.h"
#include "ffmpeg_file.h"
#include "ffmpeg_scale.h"

int g_card_fd = 0;
std::vector<drmModeConnector*> g_connectors;
std::vector<DUMB_BUFFER> g_dumb_buffers;
int g_front_buffer = 0;
drmModeCrtc* g_old_crtc;
int g_frame_nr = 0;
FFMPEG_FILE* ffmpeg_file = nullptr;
std::thread* audio_T = nullptr;
bool audio_close = false;
const char* audio_device = "default";
bool process_close = false;
std::chrono::high_resolution_clock::time_point t0;
double elapsed_time = 0.0f;
AVFrame* current_frame = nullptr;
uint8_t* current_frame_buf = nullptr;

void draw();
void flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void* user_data);
void audio_playback_T();

void sigint_handler(int sig) {

	signal(sig, SIG_IGN);

	process_close = true;

	return;

}

int main(int argc, const char* argv[]) {

	int ret = 0;

	if (argc != 3) {
	
		std::cout << "Invalid args." << std::endl;
		return 1;

	}

	std::cout << "argv[1]: " << argv[1] << std::endl; 

	g_card_fd = open(argv[1], O_RDWR | O_CLOEXEC);

	if (g_card_fd == -1) {
	
		std::cout << "Couldn't open file descriptor." << std::endl;
		return 1;

	}

    // -- Open video file 
    
    ffmpeg_file = new FFMPEG_FILE(argv[2]);
	ffmpeg_file->AsyncDecode();

	// TODO: check for dumb buffer capabilities

	// -- Install SIGINT handler

	signal(SIGINT, sigint_handler);

	// TODO: error handling

	drmModeRes* resources = drmModeGetResources(g_card_fd);

	std::cout << "count_connectors: " << resources->count_connectors << std::endl;
	std::cout << "min_width: " << resources->min_width << std::endl;
	std::cout << "max_width: " << resources->max_width << std::endl;
	std::cout << "min_height: " << resources->min_height << std::endl;
	std::cout << "max_height: " << resources->max_height << std::endl;

	for (int i = 0; i < resources->count_connectors; ++i) {
		
		drmModeConnector* connector = drmModeGetConnector(g_card_fd, resources->connectors[i]);

		if ((connector->connection == DRM_MODE_CONNECTED) && (connector->count_modes != 0)) {
		
			std::cout << "connector " << i << " is DRM_MODE_CONNECTED and count_modes > 0" << std::endl;
			g_connectors.push_back(connector);

		}

	}

	std::cout << "connectors.size(): " << g_connectors.size() << std::endl;

	drmModeModeInfo* mode_info = &g_connectors[0]->modes[0];

	std::cout << "hdisplay: " << mode_info->hdisplay << std::endl;
	std::cout << "vdisplay: " << mode_info->vdisplay << std::endl;

	drmModeEncoder* encoder = drmModeGetEncoder(g_card_fd, g_connectors[0]->encoder_id);

	uint32_t crtc_id = encoder->crtc_id;

	drmModeFreeEncoder(encoder);

	g_old_crtc = drmModeGetCrtc(g_card_fd, crtc_id);

	std::cout << "crtc_id: " << g_old_crtc->crtc_id << std::endl;

	// -- Construct DUMB_BUFFER
	
 	g_dumb_buffers.emplace_back(mode_info->vdisplay, mode_info->hdisplay, 32);
 	g_dumb_buffers.emplace_back(mode_info->vdisplay, mode_info->hdisplay, 32);
	
	int init_ret1 = g_dumb_buffers[0].init(g_card_fd);
	int init_ret2 = g_dumb_buffers[1].init(g_card_fd);

	if (init_ret1 == -1 || init_ret2 == -1) {
		g_dumb_buffers.clear();
		close(g_card_fd);
		return -1;
	}

	// -- Initial modeset

	drmModeSetCrtc(g_card_fd, crtc_id, g_dumb_buffers[1].fb_handle, 0, 0, &g_connectors[0]->connector_id, 1, mode_info);
	
	// --
	
	struct timeval timeout;
	fd_set fds;
	drmEventContext event_context;

	FD_ZERO(&fds);
	memset(&timeout, 0, sizeof(timeout));
	memset(&event_context, 0, sizeof(event_context));

	event_context.page_flip_handler = flip_handler;
	event_context.version = 2;

	timeout.tv_sec = 5;

	// -- Start clock

	t0 = std::chrono::high_resolution_clock::now();

	// -- First draw

	draw();

	// -- Start audio playback thread
	audio_T = new std::thread(audio_playback_T);

	// while (!process_close) {
	// 	drmHandleEvent(g_card_fd, &event_context);
	// }

	while (!process_close) {

		FD_SET(g_card_fd, &fds);
		int select_ret = select(g_card_fd + 1, &fds, NULL, NULL, &timeout);

		if (select_ret < 0) {
			fprintf(stderr, "select() failed. %m\n", errno);
			break;
		}
		else {
			drmHandleEvent(g_card_fd, &event_context);
		}

	}

	drmModeSetCrtc(g_card_fd, g_old_crtc->crtc_id, g_old_crtc->buffer_id, g_old_crtc->x, g_old_crtc->y, &g_connectors[0]->connector_id, 1, &g_old_crtc->mode);

	g_dumb_buffers.clear();
	close(g_card_fd);

	// -- Join audio playback thread
	audio_close = true;
	audio_T->join();
	delete audio_T;

	ffmpeg_file->StopAsyncDecode();
	delete ffmpeg_file;

	if (current_frame != nullptr) {
		av_freep(reinterpret_cast<void*>(&current_frame_buf));
		av_frame_free(&current_frame);
	}

	return 0;

}
	
void draw() {

	DUMB_BUFFER* dumb_buffer = &g_dumb_buffers[g_front_buffer];
	g_front_buffer ^= 1;

/*
	float sin_y, sin_0;
	float x, y, z;
	uint8_t x_u8, y_u8, z_u8;
	int offset;
	int i, j;
	size_t mt_max;
	std::random_device rd;
	std::mt19937 mersenne_twister;
	float pi = 3.141592653589;
	float trig_radius;
	int trig_offset;

	// -- Draw into framebuffer
	
	mersenne_twister.seed(rd());

	mt_max = mersenne_twister.max();

	memset(dumb_buffer->fb, 0x00, dumb_buffer->size);
	
	sin_0 = (dumb_buffer->height - 10) / 2;
	trig_radius = (dumb_buffer->height - 10) / 2;
	trig_offset = 0;
	// trig_offset = (g_dumb_buffer.height - 10) / 8;
	// trig_offset *= 3;

	//printf("sin_0: %f\n", sin_0);
	for (i = 0; i < dumb_buffer->width; ++i) {
	
		sin_y = sinf(pi * ((float)(i + g_frame_nr) / (trig_radius)));
		//printf("i: %d, sin_y: %f\n", i, sin_y);
		sin_y *= trig_radius;
		if (sin_y > 0) sin_y = trig_radius - sin_y;
		else sin_y = -sin_y + trig_radius;
			
		sin_y += trig_offset;

		//printf("i: %d, sin_y: %f\n", i, sin_y);	

		offset = (int)sin_y * dumb_buffer->pitch + i * 4; 
		*(uint32_t*)&dumb_buffer->fb[offset] = 0xFF00;
	

	}
*/
	/* random colors */
	/*	
	for (i = 0; i < g_dumb_buffer.height; ++i) {
		for (j = 0; j < g_dumb_buffer.width; ++j) {
		
			// -- Normalized float values

			x = (float)mersenne_twister() / mt_max;
			y = (float)mersenne_twister() / mt_max;
			z = (float)mersenne_twister() / mt_max;
		
			// -- Convert them to 8-bit unsigned integers

			x_u8 = 0xFF * x;
			y_u8 = 0xFF * y;
			z_u8 = 0xFF * z;
			
			// -- Set framebuffer value

			offset = i * g_dumb_buffer.pitch + j * 4;

			*(uint32_t*)&g_dumb_buffer.fb[offset] = (x_u8 << 0x10) | (y_u8 << 0x08) | z_u8;

		}
	}
	*/


	// -- Video draw

	std::chrono::high_resolution_clock::time_point t1;
	std::chrono::duration<long long, std::nano> delta = t1 - t0;
	t0 = t1;

	elapsed_time += delta.count() * 1e-9;
	if (current_frame == nullptr || elapsed_time >= ffmpeg_file->video_fps) {
		if (elapsed_time >= ffmpeg_file->video_fps) elapsed_time -= ffmpeg_file->video_fps;

		if (current_frame != nullptr) {
			av_freep(reinterpret_cast<void*>(&current_frame_buf));
			av_frame_free(&current_frame);
		}

		AVFrame* src_frame = ffmpeg_file->AsyncReadVideo();
		AVFrame* current_frame = FFMPEG_SCALE::RGB(src_frame, ffmpeg_file->video_codec_context, &current_frame_buf, AV_PIX_FMT_BGRA);
		av_frame_free(&src_frame);

	}

	for (int i = 0; i < current_frame->height; ++i) {
		uint8_t* pixel_ptr = (current_frame->data[0] + i * current_frame->linesize[0]);
		int fb_offset = i * dumb_buffer->pitch;
		memcpy(dumb_buffer->fb + fb_offset, pixel_ptr, current_frame->width*4);
	}


	g_frame_nr++;

	printf("%d\n", g_frame_nr);

	drmModePageFlip(g_card_fd, g_old_crtc->crtc_id, dumb_buffer->fb_handle, DRM_MODE_PAGE_FLIP_EVENT, NULL);

}

void flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void* user_data) {

	printf("YO YA\n");

	draw();

	return;

}

void audio_playback_T() {

	int err;
	unsigned int i;
	snd_pcm_t *handle;
	snd_pcm_sframes_t frames;

	if ((err = snd_pcm_open(&handle, audio_device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		return;
	}

	if ((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 6, 48000, 1, 500000)) < 0) {   /* 0.5sec */
		printf("Playback open error: %s\n", snd_strerror(err));
		return;
	}

	AVFrame* frame = nullptr;

	while (!audio_close) {

		frame = ffmpeg_file->AsyncReadAudio(); 

		int sample_size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
		int buffer_size = sample_size * frame->ch_layout.nb_channels * frame->nb_samples;
		int padded_buffer_size = ((buffer_size / 4096) * 4096) + 4096;
		uint8_t buf[padded_buffer_size];
		uint8_t* buf_pos = &buf[0];
		int offset = 0;

		for (int i = 0; i < frame->nb_samples; ++i) {
			for (int j = 0; j < frame->ch_layout.nb_channels; ++j) {
				memcpy(buf_pos, &frame->extended_data[j][offset], sample_size);
				buf_pos += sample_size;
			}
			offset += sample_size;
		}

		frames = snd_pcm_writei(handle, buf, frame->nb_samples);
		if (frames < 0) frames = snd_pcm_recover(handle, frames, 0);
		if (frames < 0) {
			printf("snd_pcm_writei failed: %s\n", snd_strerror(frames));
			audio_close = true;
		}
		// if (frames > 0 && frames < (long)sizeof(buffer)) printf("Short write (expected %li, wrote %li)\n", (long)sizeof(buffer), frames);
	
	}

	err = snd_pcm_drain(handle);
	if (err < 0) printf("snd_pcm_drain failed: %s\n", snd_strerror(err));
	snd_pcm_close(handle);

	if (frame != nullptr) av_frame_free(&frame);

	return;

}

