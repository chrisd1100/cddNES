#include "render.h"

#include <stdlib.h>

#if defined(_WIN32)
	#include "d3d9.h"
	#include "d3d11.h"
	#include "d3d12.h"

#elif defined(__APPLE__)
	#include "metal.h"
#endif

#include "ui.h"
#include "gl.h"

struct render_callbacks {
	int32_t (*init)(struct render_mod **mod_out, void *window, bool vsync,
		uint32_t width, uint32_t height, enum sampler sampler);
	void (*destroy)(struct render_mod **mod_out);
	void (*get_device)(struct render_mod *mod, struct render_device **device, struct render_context **context);
	void (*draw)(struct render_mod *mod, uint32_t window_w, uint32_t window_h, uint32_t *pixels, uint32_t aspect);
	enum ParsecStatus (*submit_parsec)(struct render_mod *mod, ParsecDSO *parsec);
	void (*present)(struct render_mod *mod);
	void (*sampler)(struct render_mod *mod, enum sampler sampler);
};

struct render {
	enum render_mode mode;
	struct render_mod *mod;
	struct render_callbacks cbs;
	struct ui *ui;
};

static struct render_callbacks CBS[] = {
	[RENDER_GL]    = {gl_init,    gl_destroy,    gl_get_device,    gl_draw,    gl_submit_parsec,    gl_present,    gl_set_sampler},

	#if defined(_WIN32)
	[RENDER_D3D9]  = {d3d9_init,  d3d9_destroy,  d3d9_get_device,  d3d9_draw,  d3d9_submit_parsec,  d3d9_present,  d3d9_set_sampler},
	[RENDER_D3D11] = {d3d11_init, d3d11_destroy, d3d11_get_device, d3d11_draw, d3d11_submit_parsec, d3d11_present, d3d11_set_sampler},
	[RENDER_D3D12] = {d3d12_init, d3d12_destroy, d3d12_get_device, d3d12_draw, d3d12_submit_parsec, d3d12_present, d3d12_set_sampler},

	#elif defined(__APPLE__)
	[RENDER_METAL] = {mtl_init, mtl_destroy, mtl_get_device, mtl_draw, mtl_submit_parsec, mtl_present, mtl_set_sampler},
	#endif
};

int32_t render_init(struct render **render_out, enum render_mode mode, void *window,
	bool vsync, uint32_t width, uint32_t height, enum sampler sampler)
{
	struct render *render = *render_out = calloc(1, sizeof(struct render));
	render->cbs = CBS[mode];
	render->mode = mode;

	int32_t r = render->cbs.init(&render->mod, window, vsync, width, height, sampler);

	if (r != 0)
		render_destroy(render_out);

	return r;
}

void render_destroy(struct render **render_out)
{
	if (render_out == NULL || *render_out == NULL)
		return;

	struct render *render = *render_out;

	ui_destroy(&render->ui);
	render->cbs.destroy(&render->mod);

	free(render);
	*render_out = NULL;
}

void render_draw(struct render *render, int32_t w, int32_t h, uint32_t *pixels, uint32_t aspect)
{
	render->cbs.draw(render->mod, w, h, pixels, aspect);
}

enum ParsecStatus render_submit_parsec(struct render *render, ParsecDSO *parsec)
{
	return render->cbs.submit_parsec(render->mod, parsec);
}

void render_present(struct render *render)
{
	render->cbs.present(render->mod);
}

void render_set_sampler(struct render *render, enum sampler sampler)
{
	render->cbs.sampler(render->mod, sampler);
}



/*** UI ***/

void render_ui_init(struct render *render, SDL_Window *window, struct ui_cbs *cbs, void *opaque)
{
	if (window) {
		struct render_device *device = NULL;
		struct render_context *context = NULL;
		render->cbs.get_device(render->mod, &device, &context);

		ui_init(&render->ui, window, cbs, opaque, render->mode, device, context);
	}
}

void render_ui_draw(struct render *render, SDL_Window *window, struct ui_props *props)
{
	if (render->ui && window) {
		struct render_device *device = NULL;
		struct render_context *context = NULL;
		render->cbs.get_device(render->mod, &device, &context);

		ui_new_frame(render->ui, device, context, window, props);
	}
}

void render_ui_set_popup(struct render *render, char *message, int32_t timeout)
{
	if (render->ui)
		ui_set_popup(render->ui, message, timeout);
}

void render_ui_sdl_input(struct render *render, SDL_Event *event)
{
	if (render->ui)
		ui_sdl_input(render->ui, event);
}

bool render_ui_block_keyboard(struct render *render)
{
	return render->ui ? ui_block_keyboard(render->ui) : false;
}
