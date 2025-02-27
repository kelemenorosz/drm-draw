#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "dumb_buffer.h"

/* init()
 *
 * returns -1 in case of error
 * always call this function when using DUMB_BUFFERs
 * */
int DUMB_BUFFER::init(int fd) {

	int ret = 0;
	int ioctl_ret = 0;
	int drm_ret = 0;
	
	struct drm_mode_map_dumb map_desc = {};
	memset(&map_desc, 0, sizeof(map_desc));
	
	// -- Create dumb buffer

	ioctl_ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &height);
	if (ioctl_ret < 0) {
	
		fprintf(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed. %m\n", errno);
		return -1;

	}

	// -- Create framebuffer

	drm_ret = drmModeAddFB(fd, width, height, 24, bpp, pitch, handle, &fb_handle);
	if (drm_ret) {
	
		fprintf(stderr, "Failed create framebuffer. %m\n", errno);
		ret = -1;
		goto destroy_db;

	}

	// -- Map framebuffer
	
	map_desc.handle = handle;

	ioctl_ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_desc);	
	if (ioctl_ret < 0) {
	
		fprintf(stderr, "DRM_IOCTL_MODE_MAP_DUMB failed.%m\n", errno);
		ret = -1;
		goto destroy_fb;
		
	}

	fb = (uint8_t*)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_desc.offset);
	if (fb == MAP_FAILED) {
	
		fprintf(stderr, "mmap failed.%m\n", errno);
		ret = -1;
		goto destroy_fb;

	}

	memset(fb, 0x00, size);

	// -- return OK

	state = ACTIVE;
	this->fd = fd;

	return 0;

	// -- Cleanup

destroy_fb:

	drmModeRmFB(fd, fb_handle);

destroy_db:

	struct drm_mode_destroy_dumb destroy_desc = {};
	destroy_desc.handle = handle;

	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_desc);

	// -- return BAD

	state = DEFAULT;

	return ret;
}

DUMB_BUFFER::~DUMB_BUFFER() {

	if (state == DEFAULT) {
		return;
	}
	
	// -- Cleanup

	drmModeRmFB(fd, fb_handle);

	struct drm_mode_destroy_dumb destroy_desc = {};
	destroy_desc.handle = handle;

	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_desc);
	
	return;

}
