#include "ui.h"

#include <stdio.h>
#include <time.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_cdd_sdl.h"
#include "imgui/imgui_impl_opengl3.h"

#if defined(_WIN32)
	#include <d3d9.h>
	#include <d3d11.h>
	#include "imgui/imgui_impl_dx9.h"
	#include "imgui/imgui_impl_dx11.h"
	#if defined(__x86_64__)
	#include "ui-d3d12-shim.h"
	#endif

#elif defined(__APPLE__)
	#include "ui-metal-shim.h"
#endif

#include "../fs.h"
#include "../api.h"

#define WINDOW_MARGIN_L   30.0f
#define WINDOW_MARGIN_TOP 70.0f
#define POPUP_MARGIN_TOP  30.0f
#define POPUP_MESSAGE_LEN 1024

struct ui {
	enum render_mode mode;
	void *opaque;
	struct ui_cbs cbs;

	// menu component
	bool menu;

	// share component
	bool share;
	bool share_open;
	char code[CODE_LEN];

	// popup component
	bool popup;
	uint32_t popup_start;
	uint32_t popup_timeout;
	char popup_message[POPUP_MESSAGE_LEN];

	// open_rom component
	bool open_rom;
	bool refresh_dir;
	char dir[MAX_FILE_NAME];
	struct finfo *fi;
	uint32_t fi_n;

	// login component
	bool login;
	bool have_code;
	time_t poll_time;
	char login_code[CODE_LEN];
	char hash[HASH_LEN];

	//windows
	#if defined(_WIN32) && defined(__x86_64__)
	struct ui_d3d12_shim *d3d12_shim;
	#endif
};



/*** COMMANDS ***/

void ui_set_popup(struct ui *ctx, const char *message, int32_t timeout)
{
	snprintf(ctx->popup_message, POPUP_MESSAGE_LEN, "%s", message);

	ctx->popup_start = SDL_GetTicks();
	ctx->popup_timeout = timeout;
	ctx->popup = true;
}

void ui_sdl_input(struct ui *ctx, SDL_Event *event)
{
	ImGui_ImplcddSDL_ProcessEvent(event);

	// Hotkeys
	if (event->type == SDL_KEYDOWN) {
		ImGuiIO &io = ImGui::GetIO();
		bool ctrl = io.KeysDown[SDL_SCANCODE_LCTRL] || io.KeysDown[SDL_SCANCODE_RCTRL];

		// Toggle Menu
		if (io.KeysDown[SDL_SCANCODE_ESCAPE]) {
			if (ctx->share || ctx->open_rom || ctx->login) {
				ctx->share = ctx->open_rom = ctx->login = ctx->have_code = false;
			} else {
				ctx->menu = !ctx->menu;
			}
		}

		// Open ROM
		if (ctrl && io.KeysDown[SDL_SCANCODE_O]) {
			ctx->refresh_dir = true;
			ctx->open_rom = !ctx->open_rom;
		}

		// NES Reset
		if (ctrl && io.KeysDown[SDL_SCANCODE_R])
			ctx->cbs.reset(ctx->opaque);
	}
}

bool ui_block_keyboard(struct ui *ctx)
{
	return ctx->login || ctx->open_rom;
}



/*** MENU COMPONENT ***/

static void ui_set_player(int32_t *pairing, int32_t position, int32_t id)
{
	for (int32_t x = 0; x < 4; x++) {
		if (pairing[x] == id) {
			pairing[x] = pairing[position];
			break;
		}
	}

	pairing[position] = id;
}

static void ui_menu(struct ui *ctx, struct ui_props *props)
{
	if (ImGui::BeginMainMenuBar()) {
		// File Menu
		if (ImGui::BeginMenu("File", true)) {
			if (ImGui::MenuItem("Open ROM", "Ctrl+O")) {
				ctx->refresh_dir = true;
				ctx->open_rom = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Exit"))
				ctx->cbs.exit(ctx->opaque);

			ImGui::EndMenu();
		}

		// NES Menu
		if (ImGui::BeginMenu("NES", true)) {
			if (ImGui::MenuItem("Reset", "Ctrl+R"))
				ctx->cbs.reset(ctx->opaque);

			ImGui::EndMenu();
		}

		// Video Menu
		if (ImGui::BeginMenu("Video", true)) {
			// Render Mode
			if (ImGui::MenuItem("OpenGL", "", props->mode == RENDER_GL, true))
				ctx->cbs.mode(RENDER_GL, ctx->opaque);

			#if defined(_WIN32)
			if (ImGui::MenuItem("DirectX 9", "", props->mode == RENDER_D3D9, true))
				ctx->cbs.mode(RENDER_D3D9, ctx->opaque);

			if (ImGui::MenuItem("DirectX 11", "", props->mode == RENDER_D3D11, true))
				ctx->cbs.mode(RENDER_D3D11, ctx->opaque);

			if (ImGui::MenuItem("DirectX 12", "", props->mode == RENDER_D3D12, true))
				ctx->cbs.mode(RENDER_D3D12, ctx->opaque);
			#endif

			#if defined(__APPLE__)
			if (ImGui::MenuItem("Metal", "", props->mode == RENDER_METAL, true))
				ctx->cbs.mode(RENDER_METAL, ctx->opaque);
			#endif

			// Vsync
			ImGui::Separator();
			if (ImGui::MenuItem("Vsync", "", props->vsync, true))
				ctx->cbs.vsync(!props->vsync, ctx->opaque);

			// Filters
			ImGui::Separator();
			if (ImGui::MenuItem("Linear", "", props->sampler == SAMPLE_LINEAR, true))
				ctx->cbs.sampler(SAMPLE_LINEAR, ctx->opaque);

			if (ImGui::MenuItem("Nearest", "", props->sampler == SAMPLE_NEAREST, true))
				ctx->cbs.sampler(SAMPLE_NEAREST, ctx->opaque);

			// Aspect Ratio
			ImGui::Separator();
			if (ImGui::MenuItem("127:105", "", props->aspect == ASPECT_PACKED(127, 105), true))
				ctx->cbs.aspect(ASPECT_PACKED(127, 105), ctx->opaque);

			if (ImGui::MenuItem("16:15", "", props->aspect == ASPECT_PACKED(16, 15), true))
				ctx->cbs.aspect(ASPECT_PACKED(16, 15), ctx->opaque);

			if (ImGui::MenuItem("8:7", "", props->aspect == ASPECT_PACKED(8, 7), true))
				ctx->cbs.aspect(ASPECT_PACKED(8, 7), ctx->opaque);

			if (ImGui::MenuItem("4:3", "", props->aspect == ASPECT_PACKED(4, 3), true))
				ctx->cbs.aspect(ASPECT_PACKED(4, 3), ctx->opaque);

			// Overscan
			ImGui::Separator();

			const char *labels[4] = {"Overscan Top", "Overscan Right", "Overscan Bottom", "Overscan Left"};
			int32_t *overscan = (int32_t *) &props->overscan;

			for (int32_t x = 0; x < 4; x++) {
				if (ImGui::BeginMenu(labels[x], true)) {
					if (ImGui::MenuItem("None", "", overscan[x] == 0, true))
						ctx->cbs.overscan(x, 0, ctx->opaque);

					if (ImGui::MenuItem("8px", "", overscan[x] == 8, true))
						ctx->cbs.overscan(x, 8, ctx->opaque);

					if (ImGui::MenuItem("16px", "", overscan[x] == 16, true))
						ctx->cbs.overscan(x, 16, ctx->opaque);

					ImGui::EndMenu();
				}
			}

			ImGui::EndMenu();
		}

		// Audio Menu
		if (ImGui::BeginMenu("Audio", true)) {
			// Stereo Toggle
			if (ImGui::MenuItem("Stereo", "", props->stereo, true))
				ctx->cbs.stereo(ctx->opaque);

			// Sample Rates
			uint32_t rates[] = {48000, 44100, 22050, 16000, 11025, 8000};

			ImGui::Separator();
			for (size_t x = 0; x < sizeof(rates) / sizeof(uint32_t); x++) {
				char label[32];
				snprintf(label, 32, "%u Hz", rates[x]);

				if (ImGui::MenuItem(label, "", props->sample_rate == rates[x], true))
					ctx->cbs.sample_rate(rates[x], ctx->opaque);
			}

			ImGui::EndMenu();
		}

		// Parsec Menu
		if (props->parsec) {
			if (ImGui::BeginMenu("Parsec", true)) {
				ParsecGuest *guests = NULL;
				uint32_t num_guests = ParsecHostGetGuests(props->parsec, GUEST_CONNECTED, &guests);

				// Login
				if (!props->logged_in) {
					if (ImGui::MenuItem("Login"))
						ctx->login = true;

				} else {
					// Logout
					if (ImGui::MenuItem("Logout")) {
						ctx->cbs.host(false, true, ctx->opaque);

						ui_set_popup(ctx, "Successfully logged out.", POPUP_TIMEOUT);
					}

					// Hosting Toggle
					if (ImGui::MenuItem("Hosting", "", props->hosting, true)) {
						bool hosting = !props->hosting;
						ParsecStatus p = ctx->cbs.host(hosting, false, ctx->opaque);

						if (p != PARSEC_OK) {
							ui_set_popup(ctx, "There was an error enabling hosting.", POPUP_TIMEOUT);

						} else if (p == PARSEC_OK && hosting) {
							ui_set_popup(ctx, "Hosting is now enabled.", POPUP_TIMEOUT);

						} else if (p == PARSEC_OK && !hosting) {
							ui_set_popup(ctx, "Hosting is now disabled.", POPUP_TIMEOUT);
						}
					}

					if (props->hosting) {
						// Shareable Link
						if (ImGui::MenuItem("Get Shareable Link"))
							ctx->share = true;

						// Kick Guests
						if (num_guests > 0) {
							ImGui::Separator();
							if (ImGui::BeginMenu("Kick", true)) {
								for (uint32_t x = 0; x < num_guests; x++)
									if (ImGui::MenuItem(guests[x].name))
										ParsecHostKickGuest(props->parsec, guests[x].id);

								ImGui::EndMenu();
							}
						}


						// Assign Controllers
						ImGui::Separator();
						for (int32_t x = 0; x < 4; x++) {
							char playernum[12];
							snprintf(playernum, 12, "Player %d", x + 1);

							if (ImGui::BeginMenu(playernum, true)) {
								if (ImGui::MenuItem("Unassigned", "", props->pairing[x] == 0, true))
									props->pairing[x] = 0;

								if (ImGui::MenuItem("Local", "", props->pairing[x] == -1, true))
									ui_set_player(props->pairing, x, -1);

								for (uint32_t y = 0; y < num_guests; y++) {
									if (ImGui::MenuItem(guests[y].name, "", (int32_t) guests[y].id == props->pairing[x], true))
										ui_set_player(props->pairing, x, guests[y].id);
								}

								ImGui::EndMenu();
							}
						}
					}
				}

				ImGui::EndMenu();
				free(guests);
			}
		}

		ImGui::EndMainMenuBar();
	}
}



/*** LOGIN COMPONENT ***/

static void ui_poll_code(struct ui *ctx)
{
	time_t now;
	time(&now);

	if (now - ctx->poll_time > 5) {
		int32_t r = ctx->cbs.poll_code(ctx->hash, ctx->opaque);

		if (r == 201) {
			ui_set_popup(ctx, "Successfully logged in.", POPUP_TIMEOUT);
			ctx->login = ctx->have_code = false;
			ctx->share = true;

		} else if (r == 202) {
			// keep polling

		} else {
			ui_set_popup(ctx, "Error logging in.", POPUP_TIMEOUT);
			ctx->login = ctx->have_code = false;
		}

		ctx->poll_time = now;
	}
}

static void ui_login(struct ui *ctx)
{
	ImGui::SetNextWindowPos(ImVec2(WINDOW_MARGIN_L, WINDOW_MARGIN_TOP));

	if (ImGui::Begin("Login", NULL, ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings)) {

		ImGui::Text("Play multiplayer NES games with friends.");
		ImGui::Text("Visit parsec.gg/activate with the code below.");

		if (!ctx->have_code) {
			ctx->have_code = ctx->cbs.login(ctx->login_code, ctx->hash, ctx->opaque);
			time(&ctx->poll_time);
		}

		if (!ctx->have_code) {
			ui_set_popup(ctx, "Error logging in.", POPUP_TIMEOUT);
			ctx->login = false;

		} else {
			ui_poll_code(ctx);
		}

		ImGui::InputText("", ctx->login_code, sizeof(ctx->login_code), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);

		if (ImGui::Button("Cancel"))
			ctx->login = ctx->have_code = false;

		ImGui::SameLine();

		ImGui::End();
	}
}



/*** POPUP COMPONENT ***/

static void ui_popup(struct ui *ctx)
{
	ImGui::SetNextWindowPos(ImVec2(WINDOW_MARGIN_L, POPUP_MARGIN_TOP));

	if (ImGui::Begin("Notify", NULL, ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav)) {

		ImGui::Text("%s", ctx->popup_message);

		ImGui::End();
	}
}



/*** OPEN ROM COMPONENT ***/

static void ui_open_rom(struct ui *ctx)
{
	ImGui::SetNextWindowPos(ImVec2(WINDOW_MARGIN_L, WINDOW_MARGIN_TOP));
	ImGui::SetNextWindowSize(ImVec2(400.0f, 550.0f));

	if (ImGui::Begin("OpenROM", NULL, ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings)) {

		if (ImGui::Button("Close Dialog"))
			ctx->open_rom = false;

		if (ctx->refresh_dir) {
			free(ctx->fi);
			ctx->fi_n = fs_list(ctx->dir, &ctx->fi);
			ctx->refresh_dir = false;
		}

		for (uint32_t x = 0; x < ctx->fi_n; x++) {
			if (ImGui::Selectable(ctx->fi[x].name)) {
				if (ctx->fi[x].dir) {
					fs_path(ctx->dir, ctx->dir, ctx->fi[x].name);
					ctx->refresh_dir = true;

				} else {
					ctx->cbs.open(ctx->dir, ctx->fi[x].name, ctx->opaque);
					ctx->open_rom = false;
				}
			}
		}

		ImGui::End();
	}
}



/*** SHARE COMPONENT ***/

static void ui_share(struct ui *ctx)
{
	ImGui::SetNextWindowPos(ImVec2(WINDOW_MARGIN_L, WINDOW_MARGIN_TOP));

	if (ImGui::Begin("OpenROM", NULL, ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings)) {

		if (!ctx->share_open) {
			if (!ctx->cbs.invite(ctx->code, ctx->opaque))
				snprintf(ctx->code, 10, "ERROR");

			ctx->share_open = true;
		}

		char link[128];
		snprintf(link, 128, "parsec.gg/g/%s", ctx->code);

		ImGui::Text("Copy and paste this link to friends to allow them to join.");
		ImGui::Text("You can get a new link at any time in the Parsec menu.");

		ImGui::SetNextItemWidth(350);
		ImGui::InputText("", link, sizeof(link), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);

		if (ImGui::Button("Got It")) {
			ctx->share = false;
			ctx->share_open = false;
		}

		ImGui::End();
	}
}



/*** INIT & FRAME ***/

void ui_init(struct ui **ctx_out, SDL_Window *window, struct ui_cbs *cbs, void *opaque,
	enum render_mode mode, struct render_device *device, struct render_context *context)
{
	device, context;

	struct ui *ctx = *ctx_out = (struct ui *) calloc(1, sizeof(struct ui));
	ctx->opaque = opaque;
	ctx->cbs = *cbs;
	ctx->mode = mode;
	ctx->menu = true;

	fs_cwd(ctx->dir, MAX_FILE_NAME);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = NULL;
	io.LogFilename = NULL;

	ImGui::StyleColorsDark();

	ImGui_ImplcddSDL_Init(window);

	switch (mode) {
		case RENDER_GL:    ImGui_ImplOpenGL3_Init("#version 110");                                        break;
		#if defined(_WIN32)
		case RENDER_D3D9:  ImGui_ImplDX9_Init((IDirect3DDevice9 *) device);                               break;
		case RENDER_D3D11: ImGui_ImplDX11_Init((ID3D11Device *) device, (ID3D11DeviceContext *) context); break;
		#if defined(__x86_64__)
		case RENDER_D3D12: ui_d3d12_init(device, &ctx->d3d12_shim);                                       break;
		#endif
		#elif defined(__APPLE__)
		case RENDER_METAL: ui_metal_init(device);                                                         break;
		#endif
	}
}

void ui_destroy(struct ui **ctx_out)
{
	if (ctx_out == NULL || *ctx_out == NULL)
		return;

	struct ui *ctx = *ctx_out;

	switch (ctx->mode) {
		case RENDER_GL:    ImGui_ImplOpenGL3_Shutdown();        break;
		#if defined(_WIN32)
		case RENDER_D3D9:  ImGui_ImplDX9_Shutdown();            break;
		case RENDER_D3D11: ImGui_ImplDX11_Shutdown();           break;
		#if defined(__x86_64__)
		case RENDER_D3D12: ui_d3d12_shutdown(&ctx->d3d12_shim); break;
		#endif
		#elif defined(__APPLE__)
		case RENDER_METAL: ui_metal_shutdown();                 break;
		#endif
	}

	ImGui_ImplcddSDL_Shutdown();
	ImGui::DestroyContext();

	free(ctx->fi);

	free(ctx);
	*ctx_out = NULL;
}

void ui_new_frame(struct ui *ctx, struct render_device *device, struct render_context *context,
	SDL_Window *window, struct ui_props *props)
{
	device, context;

	switch (ctx->mode) {
		case RENDER_GL:    ImGui_ImplOpenGL3_NewFrame();        break;
		#if defined(_WIN32)
		case RENDER_D3D9:  ImGui_ImplDX9_NewFrame();            break;
		case RENDER_D3D11: ImGui_ImplDX11_NewFrame();           break;
		#if defined(__x86_64__)
		case RENDER_D3D12: ui_d3d12_new_frame();                break;
		#endif
		#elif defined(__APPLE__)
		case RENDER_METAL: ui_metal_new_frame(context);         break;
		#endif
		default:                                                return;
	}

	ImGui_ImplcddSDL_NewFrame(window);
	ImGui::NewFrame();

	if (ctx->menu)
		ui_menu(ctx, props);

	if (ctx->login)
		ui_login(ctx);

	if (ctx->popup) {
		ui_popup(ctx);

		if (SDL_GetTicks() - ctx->popup_start > ctx->popup_timeout)
			ctx->popup = false;
	}

	if (ctx->open_rom)
		ui_open_rom(ctx);

	if (ctx->share)
		ui_share(ctx);

	ImGui::Render();

	switch (ctx->mode) {
		case RENDER_GL:    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());                    break;
		#if defined(_WIN32)
		case RENDER_D3D9:  ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());                        break;
		case RENDER_D3D11: ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());                       break;
		#if defined(__x86_64__)
		case RENDER_D3D12: ui_d3d12_render_draw_data(ctx->d3d12_shim, context, ImGui::GetDrawData()); break;
		#endif
		#elif defined(__APPLE__)
		case RENDER_METAL: ui_metal_render_draw_data(ImGui::GetDrawData(), device, context);          break;
		#endif
	}

	ImGui_ImplcddSDL_ClearInput();
}
