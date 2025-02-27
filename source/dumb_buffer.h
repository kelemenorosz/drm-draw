
struct DUMB_BUFFER {

private:

	enum db_state {
		DEFAULT = 0, 
		ACTIVE
	};

public:

	uint32_t height;
	uint32_t width;
	uint32_t bpp;
	uint32_t flags;

	uint32_t handle;
	uint32_t pitch;
	uint64_t size;	

	uint8_t* fb;
	uint32_t fb_handle;

	db_state state;
	int fd;

	DUMB_BUFFER() = delete;
	DUMB_BUFFER(uint32_t height, uint32_t width, uint32_t bpp) : height(height), width(width), bpp(bpp) { flags = 0; handle = 0; pitch = 0; size = 0; state = DEFAULT; };
	~DUMB_BUFFER();

	int init(int fd);

}; 
