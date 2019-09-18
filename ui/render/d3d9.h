#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "parsec-dso.h"

#include "mod.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t d3d9_init(struct render_mod **mod_out, void *window, bool vsync,
	uint32_t width, uint32_t height, enum sampler sampler);
void d3d9_destroy(struct render_mod **mod_out);
void d3d9_get_device(struct render_mod *mod, struct render_device **device, struct render_context **context);
void d3d9_draw(struct render_mod *mod, uint32_t window_w, uint32_t window_h, uint32_t *pixels, uint32_t aspect);
enum ParsecStatus d3d9_submit_parsec(struct render_mod *mod, ParsecDSO *parsec);
void d3d9_present(struct render_mod *mod);
void d3d9_set_sampler(struct render_mod *mod, enum sampler sampler);

#ifdef __cplusplus
}
#endif
