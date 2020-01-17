#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"

#include "parsec-dso.h"

#include "../src/nes.h"
#include "render/render.h"
#include "api.h"
#include "args.h"
#include "settings.h"
#include "fs.h"
#include "audio.h"

#define NES_W 256
#define NES_H 240
#define WINDOW_W (NES_W * 3)
#define WINDOW_H (NES_H * 3)
#define MULTIPLAYER 1

#define GAME_ID "1PkOI9mOWWueqygCthcfx7iFXtM"

struct cdd {
	struct nes *nes;
	struct render *render;
	struct api *api;
	struct audio_timer atimer;
	struct audio *audio;
	struct settings *settings;
	struct args args;
	ParsecDSO *parsec;
	SDL_Window *window;
	uint32_t cropped[NES_W * NES_H];
	char crc32[10];
	bool done;

	// Audio
	uint32_t sample_rate;
	bool stereo;

	// Video
	bool vsync;
	bool reset;
	uint32_t aspect;
	struct rect overscan;
	enum render_mode mode;
	enum sampler sampler;

	// Parsec
	ParsecHostConfig host_cfg;
	bool hosting;
	int32_t pairing[4];
};



/*** PARSEC ***/

static void cddnes_clean_rom_name(char *dst, char *src, size_t size)
{
	char *ptr = strchr(src, '\\');

	if (!ptr)
		ptr = strchr(src, '/');

	ptr = ptr ? ptr + 1 : NULL;

	if (!ptr)
		ptr = src;

	snprintf(dst, size, "%s", ptr);

	ptr = strpbrk(dst, "[(.");

	if (ptr)
		*ptr = '\0';

	for (int32_t x = (int32_t) strlen(dst) - 1; x >= 0; x--) {
		if (dst[x] == ' ') {
			dst[x] = '\0';
		} else {
			break;
		}
	}
}

static void cddnes_remove_pairing(int32_t *pairing, int32_t id)
{
	for (uint8_t x = 0; x < 4; x++)
		if (pairing[x] == id)
			pairing[x] = 0;
}

static void cddnes_guest_state_change(struct cdd *cdd, ParsecGuest *guest)
{
	const char *message = NULL;

	switch (guest->state) {
		case GUEST_CONNECTED:
			message = "%s has connected.";
			break;
		case GUEST_DISCONNECTED:
			cddnes_remove_pairing(cdd->pairing, guest->id);
			message = "%s has disconnected.";
			break;
		case GUEST_FAILED:
			message = "%s was unable to connect.";
			break;
		default:
			return;
	}

	char user_msg[100];
	snprintf(user_msg, sizeof(user_msg), message, guest->name);

	render_ui_set_popup(cdd->render, user_msg, POPUP_TIMEOUT); //XXX must be thread safe
}

static void cddnes_parsec_log(enum ParsecLogLevel level, char *msg, void *opaque)
{
	opaque;

	printf("[%s PARSEC] %s\n", level == LOG_DEBUG ? "D" : "I", msg);
}

static void cddnes_parsec_to_sdl(ParsecMessage *msg, SDL_Event *event)
{
	switch (msg->type) {
		case MESSAGE_KEYBOARD:
			event->type = msg->keyboard.pressed ? SDL_KEYDOWN : SDL_KEYUP;
			event->key.keysym.scancode = (SDL_Scancode) msg->keyboard.code;
			event->key.keysym.sym = SDL_GetKeyFromScancode((SDL_Keycode) msg->keyboard.code);
			break;
		case MESSAGE_GAMEPAD_BUTTON:
			event->type = msg->gamepadButton.pressed ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
			event->cbutton.button = msg->gamepadButton.button;
			event->cbutton.state = msg->gamepadButton.pressed;
			break;
		case MESSAGE_GAMEPAD_AXIS:
			event->type = SDL_JOYAXISMOTION;
			event->caxis.axis = msg->gamepadAxis.axis;
			event->caxis.value = msg->gamepadAxis.value;
			break;
		default:
			break;
	}
}



/*** UI CALLBACKS ***/

static void cddnes_open(char *path, char *name, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	if (cdd->crc32[0] != '\0')
		fs_save_sram(cdd->nes, cdd->crc32);

	cddnes_clean_rom_name(cdd->host_cfg.desc, name, HOST_DESC_LEN);

	if (cdd->parsec)
		ParsecHostSetConfig(cdd->parsec, &cdd->host_cfg, NULL);

	char full_path[MAX_FILE_NAME];
	fs_path(full_path, path, name);
	fs_load_rom(cdd->nes, full_path, cdd->crc32);
}

static void cddnes_exit(void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	cdd->done = true;
}

static void cddnes_reset(void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	nes_reset(cdd->nes, false);
}

static ParsecStatus cddnes_host(bool enabled, bool logout, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;


	if (enabled) {
		ParsecStatus p = ParsecHostStart(cdd->parsec, HOST_GAME, &cdd->host_cfg, cdd->args.session);
		cdd->hosting = (p == PARSEC_OK);

		return p;

	} else {
		ParsecHostStop(cdd->parsec);
		cdd->hosting = false;

		if (logout)
			memset(cdd->args.session, 0, SESSION_ID_LEN);
	}

	return PARSEC_OK;
}

static bool cddnes_login(char *code, char *hash, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	int32_t e = api_code(cdd->api, GAME_ID, code, hash);

	return (e == 201);
}

static int32_t cddnes_poll_code(char *hash, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	int32_t e = api_poll_code(cdd->api, hash, cdd->args.session);

	if (e == 201) {
		ParsecStatus p = cddnes_host(true, false, opaque);

		if (p == PARSEC_OK) {
			render_ui_set_popup(cdd->render, "You are now logged in and hosting.", POPUP_TIMEOUT);

		} else {
			render_ui_set_popup(cdd->render, "Unable to host. Please try logging in again.", POPUP_TIMEOUT);
		}
	}

	return e;
}

static bool cddnes_invite(char *code, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	int32_t e = api_invite(cdd->api, cdd->args.session, 600, 10, code);

	return (e >= 200 && e < 300);
}

static void cddnes_stereo(void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	cdd->stereo = !cdd->stereo;
	nes_set_stereo(cdd->nes, cdd->stereo);
}

static void cddnes_sample_rate(uint32_t sample_rate, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	cdd->sample_rate = sample_rate;
	nes_set_sample_rate(cdd->nes, sample_rate);

	audio_destroy(&cdd->audio);

	cdd->atimer = audio_timer_init(cdd->args.headless, cdd->sample_rate);
	audio_init(&cdd->audio, sample_rate);
}

static void cddnes_sampler(enum sampler sampler, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	cdd->sampler = sampler;
	render_set_sampler(cdd->render, sampler);
}

static void cddnes_mode(enum render_mode mode, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	cdd->mode = mode;
	cdd->reset = true;
}

static void cddnes_vsync(bool vsync, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	cdd->vsync = vsync;
	cdd->reset = true;
}

static void cddnes_aspect(uint32_t aspect, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	cdd->aspect = aspect;
}

static void cddnes_overscan(int32_t index, int32_t crop, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	int32_t *overscan = (int32_t *) &cdd->overscan;
	overscan[index] = crop;

	memset(cdd->cropped, 0, 4 * NES_W * NES_H);
}



/*** NES CALLBACKS ***/

static void cddnes_crop_copy(uint32_t *dest, uint32_t *src, struct rect *crop)
{
	int32_t adjx = (crop->right - crop->left) / 2;
	int32_t adjy = (crop->bottom - crop->top) / 2;

	for (int32_t row = crop->top; row < NES_H - crop->bottom; row++)
		memcpy(dest + (row + adjy) * NES_W + crop->left + adjx,
			src + row * NES_W + crop->left, 4 * (NES_W - crop->left - crop->right));
}

static void cddnes_log(char *str)
{
	printf("[D CDDNES] %s\n", str);
}

static void cddnes_new_frame(uint32_t *pixels, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	bool crop = cdd->overscan.top > 0 || cdd->overscan.right > 0
		|| cdd->overscan.bottom > 0 || cdd->overscan.left > 0;

	if (crop)
		cddnes_crop_copy(cdd->cropped, pixels, &cdd->overscan);

	int32_t w = WINDOW_W;
	int32_t h = WINDOW_H;

	if (cdd->window) {
		if (cdd->mode == RENDER_GL) {
			SDL_GL_GetDrawableSize(cdd->window, &w, &h);

		} else {
			SDL_GetWindowSize(cdd->window, &w, &h);
		}
	}

	render_draw(cdd->render, w, h, crop ? cdd->cropped : pixels, cdd->aspect);
}

static void cddnes_new_samples(int16_t *samples, size_t count, void *opaque)
{
	struct cdd *cdd = (struct cdd *) opaque;

	if (cdd->parsec)
		ParsecHostSubmitAudio(cdd->parsec, PCM_FORMAT_INT16, cdd->sample_rate,
			(uint8_t *) samples, (uint32_t) count);

	audio_timer_add_frames(&cdd->atimer, count);

	if (cdd->audio)
		audio_play(cdd->audio, &cdd->atimer, samples, count);
}



/*** MAIN ***/

static enum nes_button BUTTON_MAP[512] = {
	[SDL_SCANCODE_SEMICOLON] = NES_A,
	[SDL_SCANCODE_L]         = NES_B,
	[SDL_SCANCODE_LSHIFT]    = NES_SELECT,
	[SDL_SCANCODE_SPACE]     = NES_START,
	[SDL_SCANCODE_W]         = NES_UP,
	[SDL_SCANCODE_S]         = NES_DOWN,
	[SDL_SCANCODE_A]         = NES_LEFT,
	[SDL_SCANCODE_D]         = NES_RIGHT,
};

static enum nes_button CONTROLLER_MAP[SDL_CONTROLLER_BUTTON_MAX] = {
	[SDL_CONTROLLER_BUTTON_Y]          = NES_A,
	[SDL_CONTROLLER_BUTTON_A]          = NES_A,
	[SDL_CONTROLLER_BUTTON_X]          = NES_B,
	[SDL_CONTROLLER_BUTTON_B]          = NES_B,
	[SDL_CONTROLLER_BUTTON_BACK]       = NES_SELECT,
	[SDL_CONTROLLER_BUTTON_START]      = NES_START,
	[SDL_CONTROLLER_BUTTON_DPAD_UP]    = NES_UP,
	[SDL_CONTROLLER_BUTTON_DPAD_DOWN]  = NES_DOWN,
	[SDL_CONTROLLER_BUTTON_DPAD_LEFT]  = NES_LEFT,
	[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = NES_RIGHT,
};

static int8_t cddnes_find_pairing(int32_t *pairing, int32_t id)
{
	// search for an existing pairing
	for (int8_t x = 0; x < 4; x++) {
		if (pairing[x] == id)
			return x;
	}

	// add id to pairing map if slot is available
	for (int8_t x = 0; x < 4; x++) {
		if (pairing[x] == 0) {
			pairing[x] = id;
			return x;
		}
	}

	return -1;
}

static void cddnes_sdl_input(struct nes *nes, struct render *render, SDL_Event *event, int32_t *pairing, int32_t id)
{
	enum nes_button button = 0;
	bool down = false;

	switch (event->type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (render_ui_block_keyboard(render))
				break;

			down = (event->type == SDL_KEYDOWN);
			button = BUTTON_MAP[event->key.keysym.scancode];

			if (event->type == SDL_KEYDOWN) {
				switch (event->key.keysym.scancode) {
					case SDL_SCANCODE_1: nes_toggle_channel(nes, CHANNEL_PULSE0); break;
					case SDL_SCANCODE_2: nes_toggle_channel(nes, CHANNEL_PULSE1); break;
					case SDL_SCANCODE_3: nes_toggle_channel(nes, CHANNEL_TRIANGLE); break;
					case SDL_SCANCODE_4: nes_toggle_channel(nes, CHANNEL_NOISE); break;
					case SDL_SCANCODE_5: nes_toggle_channel(nes, CHANNEL_DMC); break;
				}
			}
			break;
		case SDL_CONTROLLERBUTTONUP:
		case SDL_CONTROLLERBUTTONDOWN:
			down = event->cbutton.state;
			button = CONTROLLER_MAP[event->cbutton.button];
			break;
	}

	if (button != 0) {
		int8_t player = MULTIPLAYER ? cddnes_find_pairing(pairing, id) : 0;

		if (player != -1)
			nes_controller(nes, player, button, down);
	}
}

static void cddnes_poll_parsec(struct nes *nes, ParsecDSO *parsec, int32_t *pairing, struct render *render)
{
	ParsecGuest guest;

	for (ParsecMessage msg; ParsecHostPollInput(parsec, 0, &guest, &msg);) {
		SDL_Event event = {0};
		cddnes_parsec_to_sdl(&msg, &event);
		render_ui_sdl_input(render, &event);
		cddnes_sdl_input(nes, render, &event, pairing, guest.id);
	}
}

static bool cddnes_poll_sdl(struct nes *nes, int32_t *pairing, struct render *render)
{
	for (SDL_Event event; SDL_PollEvent(&event);) {
		render_ui_sdl_input(render, &event);
		cddnes_sdl_input(nes, render, &event, pairing, -1);

		switch (event.type) {
			case SDL_QUIT:
				return true;
			case SDL_CONTROLLERDEVICEADDED:
				SDL_GameControllerOpen(event.cdevice.which);
				break;
			case SDL_JOYDEVICEADDED:
				SDL_JoystickOpen(event.jdevice.which);
				break;
			case SDL_JOYDEVICEREMOVED:
				SDL_JoystickClose(SDL_JoystickFromInstanceID(event.jdevice.which));
				break;
		}
	}

	return false;
}

static bool cddnes_need_delay(SDL_Window *window)
{
	SDL_DisplayMode mode = {0};
	SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode);

	return mode.refresh_rate > 62;
}

static void cddnes_delay_frame(uint64_t frame_start, bool past_buffer)
{
	double diff = 1000.0 * ((double) (SDL_GetPerformanceCounter() - frame_start)) /
		(double) SDL_GetPerformanceFrequency();
	double delay = (past_buffer ? 17.0 : 15.0) - diff;

	if (delay > 0.0)
		SDL_Delay(lrint(delay));
}

static void cddnes_load_settings(struct cdd *cdd)
{
	cdd->sample_rate = settings_get_int32(cdd->settings, "sample_rate", 44100);
	cdd->stereo = settings_get_bool(cdd->settings, "stereo", true);
	cdd->vsync = settings_get_bool(cdd->settings, "vsync", true);
	cdd->mode = settings_get_int32(cdd->settings, "mode", RENDER_GL);
	cdd->sampler = settings_get_int32(cdd->settings, "sampler", SAMPLE_NEAREST);
	cdd->aspect = settings_get_int32(cdd->settings, "aspect", ASPECT_PACKED(16, 15));
	cdd->overscan.top = settings_get_int32(cdd->settings, "overscan_top", 8);
	cdd->overscan.right = settings_get_int32(cdd->settings, "overscan_right", 0);
	cdd->overscan.bottom = settings_get_int32(cdd->settings, "overscan_bottom", 8);
	cdd->overscan.left = settings_get_int32(cdd->settings, "overscan_left", 0);
}

static void cddnes_save_settings(struct cdd *cdd)
{
	settings_set_int32(cdd->settings, "sample_rate", cdd->sample_rate);
	settings_set_bool(cdd->settings, "stereo", cdd->stereo);
	settings_set_bool(cdd->settings, "vsync", cdd->vsync);
	settings_set_int32(cdd->settings, "mode", cdd->mode);
	settings_set_int32(cdd->settings, "sampler", cdd->sampler);
	settings_set_int32(cdd->settings, "aspect", cdd->aspect);
	settings_set_int32(cdd->settings, "overscan_top", cdd->overscan.top);
	settings_set_int32(cdd->settings, "overscan_right", cdd->overscan.right);
	settings_set_int32(cdd->settings, "overscan_bottom", cdd->overscan.bottom);
	settings_set_int32(cdd->settings, "overscan_left", cdd->overscan.left);
}

static void *cddnes_get_window(struct cdd *cdd)
{
	if (!cdd->window)
		return NULL;

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(cdd->window, &info);

	#if defined(_WIN32)
	return (cdd->mode == RENDER_GL) ? (void *) cdd->window : (void *) info.info.win.window;
	#elif defined(__APPLE__)
	return (cdd->mode == RENDER_GL) ? (void *) cdd->window : (void *) info.info.cocoa.window;
	#else
	return cdd->window;
	#endif
}

int32_t main(int32_t argc, char **argv)
{
	struct cdd *cdd = calloc(1, sizeof(struct cdd));
	cdd->reset = true;

	cdd->host_cfg = (ParsecHostConfig) PARSEC_HOST_DEFAULTS;
	snprintf(cdd->host_cfg.gameID, GAME_ID_LEN, GAME_ID);
	snprintf(cdd->host_cfg.name, HOST_NAME_LEN, "cddNES");
	cdd->host_cfg.maxGuests = 4;
	cdd->host_cfg.publicGame = true;

	args_parse(argc, argv, &cdd->args);

	cdd->settings = settings_open();
	cddnes_load_settings(cdd);

	int32_t e = 0;

	cdd->atimer = audio_timer_init(cdd->args.headless, cdd->sample_rate);

	if (!cdd->args.headless) {
		e = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER);
		if (e != 0) {printf("SDL_Init=%d\n", e); goto except;}

		SDL_WindowFlags flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
		cdd->window = SDL_CreateWindow("cddNES", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOW_W, WINDOW_H, flags);
		if (cdd->window == NULL) {printf("SDL_CreateWindow=0\n"); goto except;}

		e = audio_init(&cdd->audio, cdd->sample_rate);
		if (e != 0) {printf("audio_init=%d\n", e); goto except;}
	}

	api_init(&cdd->api);

	e = ParsecInit(NULL, NULL, NULL, &cdd->parsec);
	if (e != PARSEC_OK) printf("ParsecInit=%d\n", e);

	if (cdd->parsec)
		ParsecSetLogCallback(cdd->parsec, cddnes_parsec_log, NULL);

	nes_init(&cdd->nes, cdd->sample_rate, cdd->stereo, cddnes_new_frame, cddnes_new_samples, cdd);
	nes_set_log_callback(cddnes_log);

	cddnes_clean_rom_name(cdd->host_cfg.desc, (cdd->args.rom[0] != '\0') ? cdd->args.rom : "Alfonzo Melee", HOST_DESC_LEN);
	fs_load_rom(cdd->nes, cdd->args.rom, cdd->crc32);

	if (cdd->parsec && cdd->args.session[0] != '\0')
		cddnes_host(true, false, cdd);

	while (!cdd->done) {
		// init renderer and UI or look for render mode changes
		if (cdd->reset) {
			render_destroy(&cdd->render);
			e = render_init(&cdd->render, cdd->mode, cddnes_get_window(cdd), cdd->vsync, NES_W, NES_H, cdd->sampler);
			if (e != 0) {printf("render_init=%d\n", e); goto except;}

			struct ui_cbs cbs = {.open = cddnes_open, .exit = cddnes_exit, .reset = cddnes_reset,
				.host = cddnes_host, .login = cddnes_login, .stereo = cddnes_stereo,
				.sample_rate = cddnes_sample_rate, .sampler = cddnes_sampler, .mode = cddnes_mode,
				.vsync = cddnes_vsync, .aspect = cddnes_aspect, .overscan = cddnes_overscan,
				.invite = cddnes_invite, .poll_code = cddnes_poll_code};
			render_ui_init(cdd->render, cdd->window, &cbs, cdd);

			if (cdd->mode == 0)
				render_ui_set_popup(cdd->render, "Press ESC to hide/show the menu bar.", POPUP_TIMEOUT);

			cdd->reset = false;
		}

		uint64_t frame_start = SDL_GetPerformanceCounter();

		// poll input from parsec and locally via SDL
		if (cdd->parsec) {
			cddnes_poll_parsec(cdd->nes, cdd->parsec, cdd->pairing, cdd->render);

			for (ParsecHostEvent event; ParsecHostPollEvents(cdd->parsec, 0, &event);)
				if (event.type == HOST_EVENT_GUEST_STATE_CHANGE)
					cddnes_guest_state_change(cdd, &event.guestStateChange.guest);
		}

		if (cdd->window)
			cdd->done = cddnes_poll_sdl(cdd->nes, cdd->pairing, cdd->render);

		// continue emulation, fires NES audio and frame callbacks
		nes_step(cdd->nes);

		// draws the UI overlay and fires events
		struct ui_props props = {.parsec = cdd->parsec, .pairing = cdd->pairing,
			.sample_rate = cdd->sample_rate, .stereo = cdd->stereo, .sampler = cdd->sampler,
			.mode = cdd->mode, .logged_in = cdd->args.session[0], .hosting = cdd->hosting,
			.vsync = cdd->vsync, .aspect = cdd->aspect, .overscan = cdd->overscan};
		render_ui_draw(cdd->render, cdd->window, &props);

		// submits the final render to Parsec
		if (cdd->parsec)
			render_submit_parsec(cdd->render, cdd->parsec);

		// swaps the host window
		render_present(cdd->render);

		// if vsync is off or refresh rate is high, the next frame needs to be delayed
		if (!cdd->vsync || cdd->args.headless || cddnes_need_delay(cdd->window)) {
			cddnes_delay_frame(frame_start, audio_timer_past_buffer(&cdd->atimer));
			nes_set_sample_rate(cdd->nes, cdd->sample_rate);

		// adjust sample rate to compensate for variations in refresh rate
		} else {
			nes_set_sample_rate(cdd->nes, cdd->sample_rate + audio_timer_rate_adjust(&cdd->atimer));
		}
	}

	fs_save_sram(cdd->nes, cdd->crc32);

	except:

	nes_destroy(&cdd->nes);
	ParsecDestroy(cdd->parsec);
	api_destroy(&cdd->api);
	render_destroy(&cdd->render);
	audio_destroy(&cdd->audio);

	if (cdd->window) {
		SDL_DestroyWindow(cdd->window);
		SDL_Quit();
	}

	cddnes_save_settings(cdd);
	settings_close(&cdd->settings);

	free(cdd);

	return 0;
}

#if defined(_WIN32)

#include <stdlib.h>
#include <windows.h>
#include <timeapi.h>

int32_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int32_t nCmdShow)
{
	hInstance, hPrevInstance, lpCmdLine, nCmdShow;

	timeBeginPeriod(1);

	int32_t r = main(__argc, __argv);

	timeEndPeriod(1);

	return r;
}
#endif
