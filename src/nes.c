#include "nes.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(WASM)
	#include <emscripten.h>

	#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
	#define EXPORT
#endif

#include "cart.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"

struct nes {
	uint8_t ram[0x0800];
	uint8_t io_open_bus;

	bool controller_strobe;
	uint32_t controller_state[2];
	uint32_t controller_bits[2];
	uint8_t buttons[4];
	uint8_t safe_buttons[4];

	struct cart *cart;
	struct cpu *cpu;
	struct ppu *ppu;
	struct apu *apu;

	bool odd_cycle;
	uint32_t frame_count;
	uint16_t read_addr;
	uint16_t write_addr;
	uint64_t cycle;
	uint64_t cycle_2007;

	void *opaque;
	FRAME_CALLBACK new_frame;
	SAMPLE_CALLBACK new_samples;
};


/*** LOG ***/

#define MAX_LOG_LEN 1024

static LOG_CALLBACK LOG = NULL;

EXPORT void nes_set_log_callback(LOG_CALLBACK log_callback)
{
	LOG = log_callback;
}

void nes_log(const char *fmt, ...)
{
	if (LOG) {
		va_list args;
		va_start(args, fmt);

		char str[MAX_LOG_LEN];
		vsnprintf(str, MAX_LOG_LEN, fmt, args);

		LOG(str);

		va_end(args);
	}
}


/*** CONTROLLER ***/

static void nes_controller_set_state(struct nes *nes, uint8_t player, uint8_t state)
{
	switch (player) {
		case 0:
			nes->controller_state[0] = ((nes->controller_state[0] & 0x00FFFF00) | (0x08 << 16) | state);
			break;
		case 1:
			nes->controller_state[1] = ((nes->controller_state[1] & 0x00FFFF00) | (0x04 << 16) | state);
			break;
		case 2:
			nes->controller_state[0] = ((nes->controller_state[0] & 0x000000FF) | (0x08 << 16) | (state << 8));
			break;
		case 3:
			nes->controller_state[1] = ((nes->controller_state[1] & 0x000000FF) | (0x04 << 16) | (state << 8));
			break;
	}
}

EXPORT void nes_controller(struct nes *nes, uint8_t player, enum nes_button button, bool down)
{
	if (down) {
		nes->buttons[player] |= button;

	} else {
		nes->buttons[player] &= ~button;
	}

	uint8_t prev_state = nes->safe_buttons[player];
	nes->safe_buttons[player] = nes->buttons[player];

	// cancel out up + down
	if ((nes->safe_buttons[player] & 0x30) == 0x30)
		nes->safe_buttons[player] &= 0xCF;

	// cancel out left + right
	if ((nes->safe_buttons[player] & 0xC0) == 0xC0)
		nes->safe_buttons[player] &= 0x3F;

	if (prev_state != nes->safe_buttons[player])
		nes_controller_set_state(nes, player, nes->safe_buttons[player]);
}

static uint8_t nes_controller_read(struct nes *nes, uint8_t n)
{
	if (nes->controller_strobe)
		return 0x40 | (nes->controller_state[n] & 0x01);

	uint8_t r = 0x40 | (nes->controller_bits[n] & 0x01);
	nes->controller_bits[n] = (0x80 << 24) | (nes->controller_bits[n] >> 1);

	return r;
}

static void nes_controller_write(struct nes *nes, bool strobe)
{
	if (nes->controller_strobe && !strobe) {
		nes->controller_bits[0] = nes->controller_state[0];
		nes->controller_bits[1] = nes->controller_state[1];
	}

	nes->controller_strobe = strobe;
}


/*** MEMORY READ & WRITE ***/

// https://wiki.nesdev.com/w/index.php/CPU_memory_map

uint8_t nes_read(struct nes *nes, uint16_t addr)
{
	if (addr < 0x2000) {
		return nes->ram[addr % 0x0800];

	} else if (addr < 0x4000) {
		addr = 0x2000 + addr % 8;

		// double 2007 read glitch and mapper 185 copy protection
		if (addr == 0x2007 && (nes->cycle - nes->cycle_2007 == 1 || cart_block_2007(nes->cart)))
			return ppu_read(nes->ppu, nes->cpu, nes->cart, 0x2003);

		nes->cycle_2007 = nes->cycle;
		return ppu_read(nes->ppu, nes->cpu, nes->cart, addr);

	} else if (addr == 0x4015) {
		nes->io_open_bus = apu_read_status(nes->apu, nes->cpu);
		return nes->io_open_bus;

	} else if (addr == 0x4016 || addr == 0x4017) {
		nes->io_open_bus = nes_controller_read(nes, addr & 1);
		return nes->io_open_bus;

	} else if (addr >= 0x4020) {
		bool mem_hit = false;
		uint8_t v = cart_prg_read(nes->cart, nes->cpu, addr, &mem_hit);

		if (mem_hit) return v;
	}

	return nes->io_open_bus;
}

uint8_t nes_read_dmc(struct nes *nes, uint16_t addr)
{
	if (nes->read_addr == 0x2007) {
		ppu_read(nes->ppu, nes->cpu, nes->cart, 0x2007);
		ppu_read(nes->ppu, nes->cpu, nes->cart, 0x2007);
	}

	if (nes->read_addr == 0x4016)
		nes_read(nes, 0x4016);

	return cpu_dma_dmc(nes->cpu, nes, addr, nes->write_addr != 0, nes->write_addr == 0x4014);
}

void nes_write(struct nes *nes, uint16_t addr, uint8_t v)
{
	if (addr < 0x2000) {
		nes->ram[addr % 0x800] = v;

	} else if (addr < 0x4000) {
		addr = 0x2000 + addr % 8;

		ppu_write(nes->ppu, nes->cpu, nes->cart, addr, v);
		cart_ppu_write_hook(nes->cart, addr, v); //MMC5 listens here

	} else if (addr < 0x4014 || addr == 0x4015 || addr == 0x4017) {
		nes->io_open_bus = v;
		apu_write(nes->apu, nes, nes->cpu, addr, v);

	} else if (addr == 0x4014) {
		nes->io_open_bus = v;
		cpu_dma_oam(nes->cpu, nes, v, nes->odd_cycle);

	} else if (addr == 0x4016) {
		nes->io_open_bus = v;
		nes_controller_write(nes, v & 0x01);

	} else if (addr < 0x4020) {
		nes->io_open_bus = v;

	} else {
		cart_prg_write(nes->cart, nes->cpu, addr, v);
	}
}


/*** SRAM ***/

EXPORT size_t nes_cart_sram_dirty(struct nes *nes)
{
	return cart_sram_dirty(nes->cart);
}

EXPORT void nes_cart_sram_get(struct nes *nes, uint8_t *buf, size_t size)
{
	cart_sram_get(nes->cart, buf, size);
}


/*** TIMING ***/

void nes_pre_tick_write(struct nes *nes, uint16_t addr)
{
	nes->write_addr = addr;

	nes->frame_count += ppu_step(nes->ppu, nes->cpu, nes->cart, nes->new_frame, nes->opaque);
	nes->frame_count += ppu_step(nes->ppu, nes->cpu, nes->cart, nes->new_frame, nes->opaque);
	nes->frame_count += ppu_step(nes->ppu, nes->cpu, nes->cart, nes->new_frame, nes->opaque);

	apu_step(nes->apu, nes, nes->cpu, nes->new_samples, nes->opaque);
}

void nes_post_tick_write(struct nes *nes)
{
	cart_step(nes->cart, nes->cpu);

	nes->cycle++;
	nes->odd_cycle = !nes->odd_cycle;
	nes->write_addr = 0;
}

void nes_pre_tick_read(struct nes *nes, uint16_t addr)
{
	nes->read_addr = addr;

	nes->frame_count += ppu_step(nes->ppu, nes->cpu, nes->cart, nes->new_frame, nes->opaque);
	nes->frame_count += ppu_step(nes->ppu, nes->cpu, nes->cart, nes->new_frame, nes->opaque);

	apu_step(nes->apu, nes, nes->cpu, nes->new_samples, nes->opaque);
}

void nes_post_tick_read(struct nes *nes)
{
	nes->frame_count += ppu_step(nes->ppu, nes->cpu, nes->cart, nes->new_frame, nes->opaque);

	cart_step(nes->cart, nes->cpu);

	nes->cycle++;
	nes->odd_cycle = !nes->odd_cycle;
	nes->read_addr = 0;
}

void nes_tick(struct nes *nes)
{
	nes_pre_tick_read(nes, 0);
	nes_post_tick_read(nes);
}


/*** RUN ***/

EXPORT void nes_step(struct nes *nes)
{
	for (uint32_t frame_count = nes->frame_count; frame_count == nes->frame_count;)
		cpu_step(nes->cpu, nes);
}


/*** INIT & DESTROY ***/

EXPORT void nes_init(struct nes **nes_out, uint32_t sample_rate, bool stereo,
	FRAME_CALLBACK new_frame, SAMPLE_CALLBACK new_samples, void *opaque)
{
	struct nes *nes = *nes_out = calloc(1, sizeof(struct nes));

	nes->opaque = opaque;
	nes->new_frame = new_frame;
	nes->new_samples = new_samples;

	cpu_init(&nes->cpu);
	ppu_init(&nes->ppu);
	apu_init(&nes->apu, sample_rate, stereo);
}

EXPORT void nes_set_stereo(struct nes *nes, bool stereo)
{
	apu_set_stereo(nes->apu, stereo);
}

EXPORT void nes_toggle_channel(struct nes *nes, enum apu_channel channel)
{
	apu_set_channels(nes->apu, apu_get_channels(nes->apu) ^ channel);
}

EXPORT void nes_set_sample_rate(struct nes *nes, uint32_t sample_rate)
{
	apu_set_sample_rate(nes->apu, sample_rate);
}

EXPORT void nes_destroy(struct nes **nes_out)
{
	if (!nes_out || !*nes_out) return;

	struct nes *nes = *nes_out;

	apu_destroy(&nes->apu);
	ppu_destroy(&nes->ppu);
	cpu_destroy(&nes->cpu);
	cart_destroy(&nes->cart);

	free(*nes_out);
	*nes_out = NULL;
}

EXPORT void nes_reset(struct nes *nes, bool hard)
{
	nes->odd_cycle = false;
	nes->frame_count = 0;
	nes->read_addr = nes->write_addr = 0;
	nes->cycle = nes->cycle_2007 = 0;

	if (hard)
		memset(nes->ram, 0, 0x0800);

	ppu_reset(nes->ppu);
	apu_reset(nes->apu, nes, nes->cpu, hard);
	cpu_reset(nes->cpu, nes, hard);

	ppu_step(nes->ppu, nes->cpu, nes->cart, nes->new_frame, nes->opaque);
}

EXPORT void nes_cart_load(struct nes *nes, uint8_t *rom, size_t rom_len,
	uint8_t *sram, size_t sram_len, struct nes_header *hdr)
{
	if (nes->cart)
		cart_destroy(&nes->cart);

	cart_init(&nes->cart, rom, rom_len, sram, sram_len, hdr);
	nes_reset(nes, true);
}
