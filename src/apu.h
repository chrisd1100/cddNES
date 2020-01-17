#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "nes.h"
#include "cpu.h"

struct apu;

/*** READ & WRITE ***/
uint8_t apu_read_status(struct apu *apu, struct cpu *cpu);
void apu_write(struct apu *apu, struct nes *nes, struct cpu *cpu, uint16_t addr, uint8_t v);

/*** RUN ***/
void apu_step(struct apu *apu, struct nes *nes, struct cpu *cpu, SAMPLE_CALLBACK new_samples, void *opaque);

/*** INIT & DESTROY ***/
void apu_set_stereo(struct apu *apu, bool stereo);
uint32_t apu_get_channels(struct apu *apu);
void apu_set_channels(struct apu *apu, uint32_t channels);
void apu_set_sample_rate(struct apu *apu, uint32_t sample_rate);
void apu_init(struct apu **apu_out, uint32_t sample_rate, bool stereo);
void apu_destroy(struct apu **apu_out);
void apu_reset(struct apu *apu, struct nes *nes, struct cpu *cpu, bool hard);
