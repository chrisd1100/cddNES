#pragma once

#include "parsec-dso.h"

#define POPUP_TIMEOUT 3000

#define ASPECT_RATIO(packed) \
	((float) (packed >> 16) / (float) (packed & 0xffff))

#define ASPECT_PACKED(x, y) \
	((x << 16) | y)

#define rlog(name, e) \
	printf("%s=%d [%s:%d]\n", (name), (e), __FILE__, __LINE__)

enum sampler {
	SAMPLE_NEAREST = 1,
	SAMPLE_LINEAR  = 2,
};

enum render_mode {
	RENDER_GL    = 1,

	#if defined(_WIN32)
	RENDER_D3D9  = 2,
	RENDER_D3D11 = 3,
	RENDER_D3D12 = 4,

	#elif defined(__APPLE__)
	RENDER_METAL = 2,
	#endif
};

struct render_mod;
struct render_device;
struct render_context;

#pragma pack(1)
struct rect {
	int32_t top, right, bottom, left;
};
#pragma pack()

struct ui_props {
	// Parsec
	ParsecDSO *parsec;
	int32_t *pairing;
	bool logged_in;
	bool hosting;

	// Video
	enum sampler sampler;
	enum render_mode mode;
	uint32_t aspect;
	struct rect overscan;
	bool vsync;

	// Audio
	uint32_t sample_rate;
	bool stereo;
};

struct ui_cbs {
	void (*open)(char *path, char *name, void *opaque);
	void (*exit)(void *opaque);
	void (*reset)(void *opaque);
	ParsecStatus (*host)(bool enabled, bool logout, void *opaque);
	bool (*login)(char *code, char *hash, void *opaque);
	int32_t (*poll_code)(char *hash, void *opaque);
	void (*stereo)(void *opaque);
	void (*sample_rate)(uint32_t sample_rate, void *opaque);
	void (*sampler)(enum sampler, void *opaque);
	void (*mode)(enum render_mode, void *opaque);
	void (*vsync)(bool vsync, void *opaque);
	void (*aspect)(uint32_t aspect, void *opaque);
	void (*overscan)(int32_t index, int32_t crop, void *opaque);
	bool (*invite)(char *code, void *opaque);
};
