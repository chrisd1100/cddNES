#pragma once

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

enum mem {
	RAM        = 0x10,
	CIRAM      = 0x11,
	ROM_SPRITE = 0x00,
	ROM_BG     = 0x01,
	ROM_DATA   = 0x02,
};

enum nes_button {
	NES_A       = 0x01,
	NES_B       = 0x02,
	NES_SELECT  = 0x04,
	NES_START   = 0x08,
	NES_UP      = 0x10,
	NES_DOWN    = 0x20,
	NES_LEFT    = 0x40,
	NES_RIGHT   = 0x80,
};

enum mirror {
	MIRROR_HORIZONTAL = 0x00110011,
	MIRROR_VERTICAL   = 0x01010101,
	MIRROR_SINGLE1    = 0x00000000,
	MIRROR_SINGLE0    = 0x11111111,
	MIRROR_FOUR       = 0x01230123,
	MIRROR_FOUR8      = 0x01234567,
	MIRROR_FOUR16     = 0x89ABCDEF,
};

enum apu_channel {
	CHANNEL_PULSE0   = 0x01,
	CHANNEL_PULSE1   = 0x02,
	CHANNEL_TRIANGLE = 0x04,
	CHANNEL_NOISE    = 0x08,
	CHANNEL_DMC      = 0x10,
	CHANNEL_ALL      = 0xFF,
};

#define SET_FLAG(reg, flag)   ((reg) |= (flag))
#define GET_FLAG(reg, flag)   ((reg) & (flag))
#define UNSET_FLAG(reg, flag) ((reg) &= ~(flag))

typedef void (*SAMPLE_CALLBACK)(int16_t *samples, size_t count, void *opaque);
typedef void (*FRAME_CALLBACK)(uint32_t *pixels, void *opaque);
typedef void (*LOG_CALLBACK)(char *str);

struct nes_header {
	size_t offset;
	uint8_t prg;
	uint8_t chr;
	enum mirror mirroring;
	uint16_t mapper;
	bool trainer;
	bool battery;

	bool has_nes2;
	struct {
		uint8_t submapper;
		uint32_t prg_wram;
		uint32_t prg_sram;
		uint32_t chr_wram;
		uint32_t chr_sram;
	} nes2;
};

struct nes;

/*** LOG ***/
void nes_set_log_callback(LOG_CALLBACK log_callback);
void nes_log(const char *fmt, ...);

/*** CONTROLLER ***/
void nes_controller(struct nes *nes, uint8_t player, enum nes_button button, bool down);

/*** MEMORY READ & WRITE ***/
uint8_t nes_read(struct nes *nes, uint16_t addr);
uint8_t nes_read_dmc(struct nes *nes, uint16_t addr);
void nes_write(struct nes *nes, uint16_t addr, uint8_t v);

/*** SRAM ***/
size_t nes_cart_sram_dirty(struct nes *nes);
void nes_cart_sram_get(struct nes *nes, uint8_t *buf, size_t size);

/*** TIMING ***/
void nes_pre_tick_write(struct nes *nes, uint16_t addr);
void nes_post_tick_write(struct nes *nes);
void nes_pre_tick_read(struct nes *nes, uint16_t addr);
void nes_post_tick_read(struct nes *nes);
void nes_tick(struct nes *nes);

/*** RUN ***/
void nes_step(struct nes *nes);

/*** INIT & DESTROY ***/
void nes_init(struct nes **nes_out, uint32_t sample_rate, bool stereo,
	FRAME_CALLBACK new_frame, SAMPLE_CALLBACK new_samples, void *opaque);
void nes_set_stereo(struct nes *nes, bool stereo);
void nes_toggle_channel(struct nes *nes, enum apu_channel channel);
void nes_set_sample_rate(struct nes *nes, uint32_t sample_rate);
void nes_destroy(struct nes **nes_out);
void nes_reset(struct nes *nes, bool hard);
void nes_cart_load(struct nes *nes, uint8_t *rom, size_t rom_len,
	uint8_t *sram, size_t sram_len, struct nes_header *hdr);
