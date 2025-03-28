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
std::thread* video_T = nullptr;
bool audio_close = false;
const char* audio_device = "YAYplug";
bool process_close = false;
std::chrono::high_resolution_clock::time_point t0;
double elapsed_time = 0.0f;
AVFrame* current_frame = nullptr;
uint8_t* current_frame_buf = nullptr;
float frame_duration = 0.0f;
struct timeval timeout;
fd_set fds;
drmEventContext event_context;
bool playback_paused = true;
bool audio_dropped = false;
int video_frames_written = 0;

void PrintMenu();
void draw();
void flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void* user_data);
void audio_playback_T();
void video_playback_T();

void sigint_handler(int sig) {

	signal(sig, SIG_IGN);

	process_close = true;
	audio_close = true;

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
	// ffmpeg_file->SeekVideo(180);
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

	FD_ZERO(&fds);
	memset(&timeout, 0, sizeof(timeout));
	memset(&event_context, 0, sizeof(event_context));

	event_context.page_flip_handler = flip_handler;
	event_context.version = 2;

	timeout.tv_sec = 5;

	// -- Set frame duration

	frame_duration = 1.0f / ffmpeg_file->video_fps;

	// -- Start clock

	t0 = std::chrono::high_resolution_clock::now();

	// -- First draw

	draw();

	playback_paused = false;

	// -- Start audio playback thread
	audio_T = new std::thread(audio_playback_T);

	// -- Start video playback thread
	video_T = new std::thread(video_playback_T);

	// -- Main menu
	while (!process_close) {

		PrintMenu();
		std::string input_string;
		std::string seek_string;
		int seek_sec;
		std::getline(std::cin, input_string);
		if (input_string.size() == 0) {
			// --
		}
		else {
			switch(input_string.front()) {
				case 'P':
				case 'p':
					std::cout << "Pausing." << std::endl;
					playback_paused = true;
					printf("Video frames written: %d.\n", video_frames_written);
					printf("Video playback duration: %f.\n", static_cast<float>(video_frames_written) / ffmpeg_file->video_fps);
					break;
				case 'U':
				case 'u':
					std::cout << "Unpausing." << std::endl;
					playback_paused = false;
					audio_dropped = false;
					break;
				case 'S':
				case 's':
					std::cout << "Seek in seconds: ";
					std::getline(std::cin, seek_string);
					seek_sec = std::stoi(seek_string);
					playback_paused = true;
					ffmpeg_file->SeekVideo(seek_sec);
					playback_paused = false;
					audio_dropped = false;
					break;
				default:
					std::cout << "Wrong input." << std::endl;
			}
		}

	}

	// -- Join video playback thread
	video_T->join();
	delete video_T;

	// -- Join audio playback thread
	audio_T->join();
	delete audio_T;

	drmModeSetCrtc(g_card_fd, g_old_crtc->crtc_id, g_old_crtc->buffer_id, g_old_crtc->x, g_old_crtc->y, &g_connectors[0]->connector_id, 1, &g_old_crtc->mode);

	g_dumb_buffers.clear();
	close(g_card_fd);

	ffmpeg_file->StopAsyncDecode();
	delete ffmpeg_file;

	if (current_frame != nullptr) {
		av_freep(reinterpret_cast<void*>(&current_frame_buf));
		av_frame_free(&current_frame);
	}

	printf("frame_duration: %f.\n", frame_duration);
	printf("elapsed_time: %f.\n", elapsed_time);

	return 0;

}
	
void draw() {

	DUMB_BUFFER* dumb_buffer = &g_dumb_buffers[g_front_buffer];
	g_front_buffer ^= 1;

	// -- Video draw

	std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
	std::chrono::duration<long long, std::nano> delta = t1 - t0;
	t0 = t1;

	if (!playback_paused) {

		elapsed_time += delta.count() * 1e-9;
		// printf("elapsed_time: %f.\n", elapsed_time);
		if (current_frame == nullptr || elapsed_time >= frame_duration) {

			AVFrame* src_frame = nullptr;
			src_frame = ffmpeg_file->AsyncReadVideo();
			if (src_frame != nullptr) {

				if (current_frame == nullptr) {
					// printf("Frist draw.\n");
				}
				else {

					av_freep(reinterpret_cast<void*>(&current_frame_buf));
					av_frame_free(&current_frame);
				}

				current_frame = FFMPEG_SCALE::RGB(src_frame, ffmpeg_file->GetVideoCodecContext(), &current_frame_buf, AV_PIX_FMT_BGRA);
				av_frame_free(&src_frame);

				video_frames_written++;

			}

			if (elapsed_time >= frame_duration) {
				elapsed_time -= frame_duration;
			}

		}

	}

	if (current_frame != nullptr) {

		for (int i = 0; i < current_frame->height; ++i) {
			uint8_t* pixel_ptr = (current_frame->data[0] + i * current_frame->linesize[0]);
			int fb_offset = i * dumb_buffer->pitch;
			memcpy(dumb_buffer->fb + fb_offset, pixel_ptr, current_frame->width*4);
		}

	}

	g_frame_nr++;

	// printf("%d\n", g_frame_nr);

	drmModePageFlip(g_card_fd, g_old_crtc->crtc_id, dumb_buffer->fb_handle, DRM_MODE_PAGE_FLIP_EVENT, NULL);

}

void flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void* user_data) {

	// printf("YO YA\n");

	draw();

	return;

}

void audio_playback_T() {

	int err;
	unsigned int i;
	snd_pcm_t *pcm_handle;
	snd_pcm_sframes_t frames;
	int ret;

	// if ((err = snd_pcm_open(&handle, audio_device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
	// 	printf("Playback open error: %s\n", snd_strerror(err));
	// 	return;
	// }

	// if ((err = snd_pcm_set_params(handle, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 6, 48000, 1, 500000)) < 0) {   /* 0.5sec */
	// 	printf("Playback open error: %s\n", snd_strerror(err));
	// 	return;
	// }

	if ((ret = snd_pcm_open(&pcm_handle, audio_device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("snd_pcm_open() failed. %s.\n", snd_strerror(ret));
	}

	// TODO: error checking
	snd_pcm_hw_params_t* pcm_hw_params;
	snd_pcm_hw_params_alloca(&pcm_hw_params);
	snd_pcm_hw_params_any(pcm_handle, pcm_hw_params);

	snd_pcm_hw_params_set_access(pcm_handle, pcm_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(pcm_handle, pcm_hw_params, SND_PCM_FORMAT_FLOAT_LE);
	snd_pcm_hw_params_set_rate(pcm_handle, pcm_hw_params, 48000, 0);
	snd_pcm_hw_params_set_channels(pcm_handle, pcm_hw_params, 6);
	snd_pcm_hw_params_set_periods(pcm_handle, pcm_hw_params, 16, 0);
	snd_pcm_hw_params_set_buffer_size(pcm_handle, pcm_hw_params, 4000);

	if ((ret = snd_pcm_hw_params(pcm_handle, pcm_hw_params)) < 0) {
		printf("snd_pcm_hw_params() failed. %s.\n", snd_strerror(ret));
	}

	AVFrame* frame = nullptr;
	int samples_written = 0;

	while (!audio_close) {

		if (!playback_paused) {

			frame = ffmpeg_file->AsyncReadAudio();
			if (frame == nullptr) continue;

			int sample_size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
			int buffer_size = sample_size * frame->ch_layout.nb_channels * frame->nb_samples;
			int padded_buffer_size = ((buffer_size / 4096) * 4096) + 4096;
			uint8_t buf[padded_buffer_size];
			uint8_t* buf_pos = &buf[0];
			int offset = 0;

			samples_written += frame->nb_samples;

			for (int i = 0; i < frame->nb_samples; ++i) {
				// for (int j = 0; j < frame->ch_layout.nb_channels; ++j) {
				// 	memcpy(buf_pos, &frame->extended_data[j][offset], sample_size);
				// 	buf_pos += sample_size;
				// }
				memcpy(buf_pos, &frame->extended_data[0][offset], sample_size);
				buf_pos += sample_size;
				memcpy(buf_pos, &frame->extended_data[1][offset], sample_size);
				buf_pos += sample_size;
				memcpy(buf_pos, &frame->extended_data[4][offset], sample_size);
				buf_pos += sample_size;
				memcpy(buf_pos, &frame->extended_data[5][offset], sample_size);
				buf_pos += sample_size;
				memcpy(buf_pos, &frame->extended_data[2][offset], sample_size);
				buf_pos += sample_size;
				memcpy(buf_pos, &frame->extended_data[3][offset], sample_size);
				buf_pos += sample_size;
				offset += sample_size;
			}

			frames = snd_pcm_writei(pcm_handle, buf, frame->nb_samples);
			if (frames < 0) frames = snd_pcm_recover(pcm_handle, frames, 0);
			if (frames < 0) {
				printf("snd_pcm_writei failed: %s\n", snd_strerror(frames));
				audio_close = true;
			}
			// if (frames > 0 && frames < (long)sizeof(buffer)) printf("Short write (expected %li, wrote %li)\n", (long)sizeof(buffer), frames);
		
		}
		else {

			if (!audio_dropped) {

				snd_pcm_drop(pcm_handle);
				snd_pcm_prepare(pcm_handle);

				snd_pcm_sframes_t avail_frames = snd_pcm_avail(pcm_handle);
				printf("Avail frames: %d.\n", static_cast<int>(avail_frames));
				printf("Samples written: %d.\n", samples_written);
				printf("Time elapsed: %f.\n", static_cast<float>(samples_written - avail_frames) / 48000.0f);

				audio_dropped = true;

			}

		}

	}

	err = snd_pcm_drain(pcm_handle);
	if (err < 0) printf("snd_pcm_drain failed: %s\n", snd_strerror(err));
	snd_pcm_close(pcm_handle);

	if (frame != nullptr) av_frame_free(&frame);

	return;

}

void video_playback_T() {

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

	return;

}

void PrintMenu() {

	printf("(P) Pause.\n");
	printf("(U) Unpause.\n");
	printf("(S) Seek.\n");
	printf("Ctrl+C to Quit.\n");

	return;

}
