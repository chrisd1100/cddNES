#include "apu.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static int16_t PULSE_TABLE[31];
static int16_t TND_TABLE[203];


/*** LENGTH COUNTER ***/

struct length {
	bool enabled;
	bool next_enabled;
	bool skip_clock;
	uint8_t value;
};

static void apu_step_length(struct length *len)
{
	if (len->skip_clock) {
		len->skip_clock = false;
		return;
	}

	if (len->enabled && len->value > 0)
		len->value--;
}


/*** ENVELOPE ***/

struct envelope {
	bool constant_volume;
	bool start;
	bool loop;
	uint8_t v;
	uint8_t divider_period;
	uint8_t decay_level;
};

static void apu_step_envelope(struct envelope *env)
{
	if (!env->start) {
		if (env->divider_period == 0) {
			env->divider_period = env->v;

			if (env->decay_level == 0) {
				if (env->loop)
					env->decay_level = 15;
			} else {
				env->decay_level--;
			}
		} else {
			env->divider_period--;
		}

	} else {
		env->start = false;
		env->decay_level = 15;
		env->divider_period = env->v;
	}
}


/*** PULSE (SQUARE) CHANNELS ***/

struct timer {
	uint16_t period;
	uint16_t value;
};

struct pulse {
	bool enabled;
	int16_t output;

	struct timer timer;
	struct length len;
	struct envelope env;

	struct {
		bool reload;
		bool enabled;
		bool negate;
		uint8_t shift;
		uint8_t period;
		uint8_t value;
	} sweep;

	uint8_t duty_mode;
	uint8_t duty_value;
};

static uint8_t LENGTH_TABLE[] = {
	10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
	12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

static uint8_t DUTY_TABLE[4][8] = {
	{0, 1, 0, 0, 0, 0, 0, 0},
	{0, 1, 1, 0, 0, 0, 0, 0},
	{0, 1, 1, 1, 1, 0, 0, 0},
	{1, 0, 0, 1, 1, 1, 1, 1},
};

static bool apu_sweep_mute(struct pulse *p)
{
	return
		p->timer.period < 8 ||
		(!p->sweep.negate && ((p->timer.period + (p->timer.period >> p->sweep.shift)) & 0x0800));
}

static void apu_pulse_step_sweep(struct pulse *p, uint8_t channel)
{
	if (p->sweep.value == 0 && p->sweep.enabled && !apu_sweep_mute(p) && p->sweep.shift > 0) {
		int32_t delta = p->timer.period >> p->sweep.shift;

		if (p->sweep.negate) {
			delta = -delta;

			if (channel == 0)
				delta--;
		}

		p->timer.period = (uint16_t) ((int32_t) p->timer.period + delta);
	}

	if (p->sweep.value == 0 || p->sweep.reload) {
		p->sweep.value = p->sweep.period;
		p->sweep.reload = false;

	} else {
		p->sweep.value--;
	}
}

static void apu_pulse_output(struct pulse *p)
{
	uint8_t level = (p->len.value == 0 || apu_sweep_mute(p) ||
		DUTY_TABLE[p->duty_mode][p->duty_value] == 0) ? 0 :
		p->env.constant_volume ? p->env.v : p->env.decay_level;

	p->output = PULSE_TABLE[level];
}

static void apu_pulse_step_timer(struct pulse *p)
{
	if (p->timer.value == 0) {
		p->timer.value = p->timer.period;
		p->duty_value = (p->duty_value + 1) % 8;

		apu_pulse_output(p);

	} else {
		p->timer.value--;
	}
}


/*** TRIANGLE CHANNEL ***/

struct triangle {
	bool enabled;
	int16_t output;
	bool pop;

	struct timer timer;
	struct length len;

	struct {
		bool reload;
		uint8_t period;
		uint8_t value;
	} counter;

	uint8_t duty_value;
};

static uint8_t TRIANGLE_TABLE[] = {
	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

static void apu_triangle_output(struct triangle *t)
{
	if (!t->pop && t->duty_value >= 15)
		t->pop = true;

	uint8_t level = t->pop ? TRIANGLE_TABLE[t->duty_value] : 0;

	t->output = TND_TABLE[level] * 3;
}

static void apu_triangle_step_timer(struct triangle *t)
{
	if (t->timer.value == 0) {
		t->timer.value = t->timer.period;

		//timer.period of 0 cause high pitched tones
		if (t->len.value > 0 && t->counter.value > 0 && t->timer.period > 0)
			t->duty_value = (t->duty_value + 1) % 32;

		apu_triangle_output(t);

	} else {
		t->timer.value--;
	}
}

static void apu_triangle_step_counter(struct triangle *t)
{
	if (t->counter.reload) {
		t->counter.value = t->counter.period;

	} else if (t->counter.value > 0) {
		t->counter.value--;
	}

	if (t->len.enabled)
		t->counter.reload = false;
}


/*** NOISE CHANNEL ***/

struct noise {
	bool enabled;
	int16_t output;

	struct timer timer;
	struct length len;
	struct envelope env;

	bool mode;
	uint16_t shift_register;
};

static uint16_t NOISE_TABLE[] = {
	4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

static void apu_noise_output(struct noise *n)
{
	uint8_t level =
		(n->len.value == 0 || (n->shift_register & 0x0001)) ? 0 :
		n->env.constant_volume ? n->env.v : n->env.decay_level;

	n->output = TND_TABLE[level] * 2;
}

static void apu_noise_step_timer(struct noise *n)
{
	if (n->timer.value > 0)
		n->timer.value--;

	if (n->timer.value == 0) {
		n->timer.value = n->timer.period;

		uint16_t feedback = (n->shift_register & 0x0001) ^ ((n->shift_register >> (n->mode ? 6 : 1)) & 0x0001);
		n->shift_register = (n->shift_register >> 1) | (feedback << 14);

		apu_noise_output(n);
	}
}


/*** DMC (SAMPLES) CHANNEL ***/

struct dmc {
	bool enabled;
	int16_t output;

	struct timer timer;

	struct {
		uint8_t shift_register;
		uint8_t bits_remaining;
		uint8_t level;
		bool silence;
	} out;

	struct {
		bool sample_buffer_empty;
		uint8_t sample_buffer;
	} reader;

	uint16_t sample_address;
	uint16_t sample_length;
	uint16_t current_address;
	uint16_t current_length;
	bool loop;
	bool irq;
	bool irq_flag;
};

static uint8_t DMC_TABLE[] = {
	214, 190, 170, 160, 143, 127, 113, 107, 95, 80, 71, 64, 53, 42, 36, 27,
};

static void apu_dmc_restart(struct dmc *d)
{
	d->current_address = d->sample_address;
	d->current_length = d->sample_length;
}

static void apu_dmc_output(struct dmc *d)
{
	d->output = TND_TABLE[d->out.level];
}

static void apu_dmc_fill_sample_buffer(struct dmc *d, struct nes *nes, struct cpu *cpu)
{
	if (d->reader.sample_buffer_empty && d->current_length > 0) {
		d->reader.sample_buffer = nes_read_dmc(nes, d->current_address);

		d->current_address = (d->current_address == 0xFFFF) ? 0x8000 : d->current_address + 1;
		d->current_length--;

		if (d->current_length == 0) {
			if (d->loop) {
				apu_dmc_restart(d);

			} else if (d->irq) {
				d->irq_flag = true;
				cpu_irq(cpu, IRQ_DMC, true);
			}
		}

		d->reader.sample_buffer_empty = false;
	}
}

static void apu_dmc_step_timer(struct dmc *d, struct nes *nes, struct cpu *cpu)
{
	if (d->timer.value > 0)
		d->timer.value--;

	if (d->timer.value == 0) {
		d->timer.value = d->timer.period;

		if (!d->out.silence) {
			if (d->out.shift_register & 0x01 && d->out.level <= 125) {
				d->out.level += 2;

			} else if (d->out.level >= 2) {
				d->out.level -= 2;
			}

			apu_dmc_output(d);
		}

		d->out.shift_register >>= 1;

		//out cycle has ended
		if (d->out.bits_remaining == 0) {
			d->out.bits_remaining = 8;

			if (d->reader.sample_buffer_empty) {
				d->out.silence = true;

			} else {
				d->out.silence = false;
				d->out.shift_register = d->reader.sample_buffer;
				d->reader.sample_buffer_empty = true;
				apu_dmc_fill_sample_buffer(d, nes, cpu);
			}
		}

		d->out.bits_remaining--;
	}
}


/*** DAC (SAMPLING) ***/

#define TIME_BITS     20
#define TIME_UNIT     (1 << TIME_BITS)
#define BASS_SHIFT    9
#define DELTA_BITS    15
#define PHASE_COUNT   32
#define CLOCK_RATE    1789773
#define OUTPUT_SIZE   1024

struct dac {
	bool stereo;
	uint32_t frame_samples;
	uint32_t factor;
	uint32_t offset;
	uint32_t cycle;
	int16_t prev_sample[2];
	int32_t integrator[2];
	int32_t samples[2][2048];
	int16_t output[OUTPUT_SIZE];
};

static int16_t SINC[PHASE_COUNT + 1][16] = {
	{43, -115,  350, -488, 1136, -914,  5861, 21022},
	{44, -118,  348, -473, 1076, -799,  5274, 21001},
	{45, -121,  344, -454, 1011, -677,  4706, 20936},
	{46, -122,  336, -431,  942, -549,  4156, 20829},
	{47, -123,  327, -404,  868, -418,  3629, 20679},
	{47, -122,  316, -375,  792, -285,  3124, 20488},
	{47, -120,  303, -344,  714, -151,  2644, 20256},
	{46, -117,  289, -310,  634,  -17,  2188, 19985},
	{46, -114,  273, -275,  553,  117,  1758, 19675},
	{44, -108,  255, -237,  471,  247,  1356, 19327},
	{43, -103,  237, -199,  390,  373,   981, 18944},
	{42,  -98,  218, -160,  310,  495,   633, 18527},
	{40,  -91,  198, -121,  231,  611,   314, 18078},
	{38,  -84,  178,  -81,  153,  722,    22, 17599},
	{36,  -76,  157,  -43,   80,  824,  -241, 17092},
	{34,  -68,  135,   -3,    8,  919,  -476, 16558},
	{32,  -61,  115,   34,  -60, 1006,  -683, 16001},
	{29,  -52,   94,   70, -123, 1083,  -862, 15422},
	{27,  -44,   73,  106, -184, 1152, -1015, 14824},
	{25,  -36,   53,  139, -239, 1211, -1142, 14210},
	{22,  -27,   34,  170, -290, 1261, -1244, 13582},
	{20,  -20,   16,  199, -335, 1301, -1322, 12942},
	{18,  -12,   -3,  226, -375, 1331, -1376, 12293},
	{15,   -4,  -19,  250, -410, 1351, -1408, 11638},
	{13,    3,  -35,  272, -439, 1361, -1419, 10979},
	{11,    9,  -49,  292, -464, 1362, -1410, 10319},
	{ 9,   16,  -63,  309, -483, 1354, -1383,  9660},
	{ 7,   22,  -75,  322, -496, 1337, -1339,  9005},
	{ 6,   26,  -85,  333, -504, 1312, -1280,  8355},
	{ 4,   31,  -94,  341, -507, 1278, -1205,  7713},
	{ 3,   35, -102,  347, -506, 1238, -1119,  7082},
	{ 1,   40, -110,  350, -499, 1190, -1021,  6464},
	{ 0,   43, -115,  350, -488, 1136,  -914,  5861},
};

static int16_t apu_clampi32(int32_t pcmi32)
{
	return pcmi32 < -32768 ? -32768 : pcmi32 > 32767 ? 32767 : (int16_t) pcmi32;
}

static int16_t apu_clampf64(double pcm)
{
	return apu_clampi32(lrint(pcm * 32768.0));
}

static void apu_dac_init(void)
{
	for (int32_t x = 0; x < 31; x++)
		PULSE_TABLE[x] = apu_clampf64(95.52 / (8128.0 / (float) x + 100.0));

	for (int32_t x = 0; x < 203; x++)
		TND_TABLE[x] = apu_clampf64(163.67 / (24329.0 / (float) x + 100.0));

	for (int32_t x = 0; x < PHASE_COUNT + 1; x++) {
		for (int32_t y = 0; y < 8; y++)
			SINC[PHASE_COUNT - x][15 - y] = SINC[x][y];
	}
}

static void apu_dac_add_sample(struct dac *dac, uint32_t offset, uint8_t chan, int16_t sample, bool fast)
{
	if (sample == dac->prev_sample[chan])
		return;

	int32_t delta = sample - dac->prev_sample[chan];
	int32_t *out = dac->samples[chan] + (offset >> TIME_BITS);

	if (fast) {
		int32_t interp = (offset >> 5) & 0x7FFF;
		int32_t delta2 = delta * interp;

		out[7] += delta * 0x8000 - delta2;
		out[8] += delta2;

	} else {
		int32_t phase = (offset >> 15) & 0x1F;
		int32_t interp = offset & 0x7FFF;
		int32_t delta2 = (delta * interp) >> DELTA_BITS;
		delta -= delta2;

		for (uint8_t x = 0; x < 16; x++)
			out[x] += SINC[phase][x] * delta + SINC[phase + 1][x] * delta2;
	}

	dac->prev_sample[chan] = sample;
}

static void apu_dac_output_channel(struct dac *dac, uint8_t chan, int32_t offset)
{
	int16_t s = apu_clampi32(dac->integrator[chan] >> DELTA_BITS);
	dac->output[offset * 2 + chan] = s;
	dac->integrator[chan] += dac->samples[chan][offset];
	dac->integrator[chan] -= s << (DELTA_BITS - BASS_SHIFT); //high pass filter
}

static void apu_dac_spatialize(struct dac *dac, int32_t x)
{
	int16_t *l = &dac->output[x * 2];
	int16_t *r = &dac->output[x * 2 + 1];

	if (dac->stereo) {
		int16_t stereo_l = (int16_t) lrint((*l * 0.65 + *r * 0.35) * 1.65);
		int16_t stereo_r = (int16_t) lrint((*r * 0.65 + *l * 0.35) * 1.65);

		*l = stereo_l;
		*r = stereo_r;

	} else {
		*r = *l;
	}
}

static void apu_dac_generate_output(struct dac *dac, uint32_t offset, SAMPLE_CALLBACK new_samples, void *opaque)
{
	int32_t samples = offset >> TIME_BITS;
	dac->offset = offset & (TIME_UNIT - 1);
	dac->cycle = 0;

	for (int32_t x = 0; x < samples; x++) {
		apu_dac_output_channel(dac, 0, x);
		apu_dac_output_channel(dac, 1, x);
		apu_dac_spatialize(dac, x);

		if (x < 18) {
			dac->samples[0][x] = dac->samples[0][samples + x];
			dac->samples[1][x] = dac->samples[1][samples + x];
			dac->samples[0][samples + x]  = dac->samples[1][samples + x] = 0;
		} else {
			dac->samples[0][x]  = dac->samples[1][x] = 0;
		}
	}

	new_samples(dac->output, samples, opaque);
}

static void apu_dac_step(struct dac *dac, int16_t l, int16_t r, SAMPLE_CALLBACK new_samples, void *opaque)
{
	uint32_t offset = dac->cycle * dac->factor + dac->offset;

	if (!dac->stereo)
		l += r;

	apu_dac_add_sample(dac, offset, 0, l, false);
	apu_dac_add_sample(dac, offset, 1, r, false);

	if (dac->cycle++ > dac->frame_samples)
		apu_dac_generate_output(dac, offset, new_samples, opaque);
}


/*** READ & WRITE ***/

struct apu {
	bool mode;
	bool next_mode;
	bool irq_disabled;
	bool frame_irq;
	uint8_t delayed_reset;
	uint32_t channels;

	int64_t cpu_cycle;
	int64_t frame_counter;

	struct pulse p[2];
	struct triangle t;
	struct noise n;
	struct dmc d;

	struct dac dac;
};

static void apu_reload_length(struct apu *apu, struct length *len, bool channel_enabled, uint8_t v)
{
	bool in_length_cycle = apu->frame_counter == 14912 || apu->frame_counter == (apu->mode ? 37280 : 29828);

	len->skip_clock = len->value == 0 && in_length_cycle;
	bool ignore_reload = len->value != 0 && in_length_cycle;

	if (channel_enabled && !ignore_reload)
		len->value = LENGTH_TABLE[v >> 3];
}

static void apu_set_frame_irq(struct apu *apu, struct cpu *cpu, bool enabled)
{
	apu->frame_irq = enabled;
	cpu_irq(cpu, IRQ_APU, enabled);
}

uint8_t apu_read_status(struct apu *apu, struct cpu *cpu)
{
	uint8_t r = 0;

	if (apu->p[0].len.value > 0)   r |= 0x01;
	if (apu->p[1].len.value > 0)   r |= 0x02;
	if (apu->t.len.value > 0)      r |= 0x04;
	if (apu->n.len.value > 0)      r |= 0x08;
	if (apu->d.current_length > 0) r |= 0x10;
	if (apu->frame_irq)            r |= 0x40;
	if (apu->d.irq_flag)           r |= 0x80;

	apu_set_frame_irq(apu, cpu, false);

	return r;
}

void apu_write(struct apu *apu, struct nes *nes, struct cpu *cpu, uint16_t addr, uint8_t v)
{
	switch (addr) {
		case 0x4000: // Pulse
		case 0x4004: {
			uint8_t p = addr == 0x4000 ? 0 : 1;

			apu->p[p].duty_mode = v >> 6;
			apu->p[p].len.next_enabled = !(v & 0x20);
			apu->p[p].env.loop = v & 0x20;
			apu->p[p].env.constant_volume = v & 0x10;
			apu->p[p].env.v = v & 0x0F;
			break;
		}
		case 0x4001:
		case 0x4005: {
			uint8_t p = addr == 0x4001 ? 0 : 1;

			apu->p[p].sweep.enabled = v & 0x80;
			apu->p[p].sweep.period = (v >> 4) & 0x07;
			apu->p[p].sweep.negate = v & 0x08;
			apu->p[p].sweep.shift = v & 0x07;
			apu->p[p].sweep.reload = true;
			break;
		}
		case 0x4002:
		case 0x4006: {
			uint8_t p = addr == 0x4002 ? 0 : 1;

			apu->p[p].timer.period = (apu->p[p].timer.period & 0xFF00) | (uint16_t) v;
			break;
		}
		case 0x4003:
		case 0x4007: {
			uint8_t p = addr == 0x4003 ? 0 : 1;

			apu_reload_length(apu, &apu->p[p].len, apu->p[p].enabled, v);
			apu->p[p].timer.period = (apu->p[p].timer.period & 0x00FF) | ((uint16_t) (v & 0x07) << 8);
			apu->p[p].env.start = true;
			apu->p[p].duty_value = 0;
			break;
		}
		case 0x4008: // Triangle
			apu->t.len.next_enabled = !(v & 0x80);
			apu->t.counter.period = v & 0x7F;
			break;
		case 0x4009:
			break;
		case 0x400A:
			apu->t.timer.period = (apu->t.timer.period & 0xFF00) | (uint16_t) v;
			break;
		case 0x400B:
			apu_reload_length(apu, &apu->t.len, apu->t.enabled, v);
			apu->t.timer.period = (apu->t.timer.period & 0x00FF) | ((uint16_t) (v & 0x07) << 8);
			apu->t.counter.reload = true;
			break;
		case 0x400C: // Noise
			apu->n.len.next_enabled = !(v & 0x20);
			apu->n.env.loop = v & 0x20;
			apu->n.env.constant_volume = v & 0x10;
			apu->n.env.v = v & 0x0F;
			break;
		case 0x400D:
			break;
		case 0x400E:
			apu->n.mode = v & 0x80;
			apu->n.timer.period = NOISE_TABLE[v & 0x0F];
			break;
		case 0x400F:
			apu_reload_length(apu, &apu->n.len, apu->n.enabled, v);
			apu->n.env.start = true;
			break;
		case 0x4010: // DMC
			apu->d.irq = v & 0x80;

			if (!apu->d.irq) {
				apu->d.irq_flag = false;
				cpu_irq(cpu, IRQ_DMC, false);
			}

			apu->d.loop = v & 0x40;
			apu->d.timer.period = DMC_TABLE[v & 0x0F];
			break;
		case 0x4011:
			apu->d.out.level = v & 0x7F;
			apu_dmc_output(&apu->d);
			break;
		case 0x4012:
			apu->d.sample_address = 0xC000 | ((uint16_t) v << 6);
			break;
		case 0x4013:
			apu->d.sample_length = ((uint16_t) v << 4) | 0x0001;
			break;
		case 0x4015: // Status
			apu->p[0].enabled = v & 0x01;
			apu->p[1].enabled = v & 0x02;
			apu->t.enabled = v & 0x04;
			apu->n.enabled = v & 0x08;
			apu->d.enabled = v & 0x10;

			apu->d.irq_flag = false;
			cpu_irq(cpu, IRQ_DMC, false);

			if (!apu->p[0].enabled)
				apu->p[0].len.value = 0;

			if (!apu->p[1].enabled)
				apu->p[1].len.value = 0;

			if (!apu->t.enabled)
				apu->t.len.value = 0;

			if (!apu->n.enabled)
				apu->n.len.value = 0;

			if (!apu->d.enabled) {
				apu->d.current_length = 0;

			} else {
				if (apu->d.current_length == 0)
					apu_dmc_restart(&apu->d);

				apu_dmc_fill_sample_buffer(&apu->d, nes, cpu);
			}
			break;
		case 0x4017: // Frame Counter
			apu->next_mode = v & 0x80;
			apu->irq_disabled = v & 0x40;
			apu->delayed_reset = (apu->cpu_cycle & 1) ? 3 : 4;

			if (apu->irq_disabled)
				apu_set_frame_irq(apu, cpu, false);
			break;
	}
}


/*** RUN ***/

static void apu_step_all_envelope(struct apu *apu)
{
	apu_step_envelope(&apu->p[0].env);
	apu_step_envelope(&apu->p[1].env);
	apu_triangle_step_counter(&apu->t);
	apu_step_envelope(&apu->n.env);

	apu_pulse_output(&apu->p[0]);
	apu_pulse_output(&apu->p[1]);
	apu_triangle_output(&apu->t);
	apu_noise_output(&apu->n);
}

static void apu_step_all_sweep_and_length(struct apu *apu)
{
	apu_pulse_step_sweep(&apu->p[0], 0);
	apu_pulse_step_sweep(&apu->p[1], 1);

	apu_step_length(&apu->p[0].len);
	apu_step_length(&apu->p[1].len);
	apu_step_length(&apu->t.len);
	apu_step_length(&apu->n.len);
}

static void apu_delayed_length_enabled(struct apu *apu)
{
	apu->p[0].len.enabled = apu->p[0].len.next_enabled;
	apu->p[1].len.enabled = apu->p[1].len.next_enabled;
	apu->t.len.enabled = apu->t.len.next_enabled;
	apu->n.len.enabled = apu->n.len.next_enabled;
}

static void apu_step_frame_counter(struct apu *apu, struct cpu *cpu)
{
	if (apu->mode) {
		switch (apu->frame_counter) {
			case 7457:
				apu_step_all_envelope(apu);
				break;
			case 14913:
				apu_step_all_sweep_and_length(apu);
				apu_step_all_envelope(apu);
				break;
			case 22371:
				apu_step_all_envelope(apu);
				break;
			case 29829:
				break;
			case 37281:
				apu_step_all_sweep_and_length(apu);
				apu_step_all_envelope(apu);
				break;
			case 37282:
				apu->frame_counter = 0;
				break;
		}
	} else {
		switch (apu->frame_counter) {
			case 7457:
				apu_step_all_envelope(apu);
				break;
			case 14913:
				apu_step_all_sweep_and_length(apu);
				apu_step_all_envelope(apu);
				break;
			case 22371:
				apu_step_all_envelope(apu);
				break;
			case 29828:
				if (!apu->irq_disabled)
					apu_set_frame_irq(apu, cpu, true);
				break;
			case 29829:
				if (!apu->irq_disabled)
					apu_set_frame_irq(apu, cpu, true);

				apu_step_all_sweep_and_length(apu);
				apu_step_all_envelope(apu);
				break;
			case 29830:
				if (!apu->irq_disabled)
					apu_set_frame_irq(apu, cpu, true);

				apu->frame_counter = 0;
				break;
		}
	}
}

void apu_step(struct apu *apu, struct nes *nes, struct cpu *cpu, SAMPLE_CALLBACK new_samples, void *opaque)
{
	apu->cpu_cycle++;
	apu->frame_counter++;

	//pulse & dmc step every other clock
	if (apu->cpu_cycle & 1) {
		apu_pulse_step_timer(&apu->p[0]);
		apu_pulse_step_timer(&apu->p[1]);
		apu_dmc_step_timer(&apu->d, nes, cpu);
	}

	//triangle & noise step every clock
	apu_triangle_step_timer(&apu->t);
	apu_noise_step_timer(&apu->n);

	//sample
	int16_t l = 0, r = 0;

	if (apu->channels & CHANNEL_PULSE0)
		l += apu->p[0].output;

	if (apu->channels & CHANNEL_PULSE1)
		r += apu->p[1].output;

	if (apu->channels & CHANNEL_TRIANGLE)
		l += apu->t.output;

	if (apu->channels & CHANNEL_NOISE)
		r += apu->n.output;

	if (apu->channels & CHANNEL_DMC)
		l += apu->d.output;

	apu_dac_step(&apu->dac, l, r, new_samples, opaque);

	//process the frame counter
	if (!(apu->delayed_reset > 0 && apu->delayed_reset < 3 && apu->mode))
		apu_step_frame_counter(apu, cpu);

	//enabling/disabling length happens with a 1 cycle delay
	apu_delayed_length_enabled(apu);

	//writing to 4017 causes a delayed reset of the frame counter
	apu->mode = apu->next_mode;
	if (apu->delayed_reset > 0) {
		if (--apu->delayed_reset == 0) {
			apu->frame_counter = 0;

			//quarter/half frame triggers up front if mode is set
			if (apu->mode) {
				apu_step_all_envelope(apu);
				apu_step_all_sweep_and_length(apu);
			}
		}
	}
}


/*** INIT & DESTROY ***/

void apu_set_stereo(struct apu *apu, bool stereo)
{
	apu->dac.stereo = stereo;
}

uint32_t apu_get_channels(struct apu *apu)
{
	return apu->channels;
}

void apu_set_channels(struct apu *apu, uint32_t channels)
{
	apu->channels = channels;
}

void apu_set_sample_rate(struct apu *apu, uint32_t sample_rate)
{
	apu->dac.factor = (uint32_t) ceil(TIME_UNIT * (double) sample_rate / (double) CLOCK_RATE);
	apu->dac.frame_samples = (CLOCK_RATE / sample_rate) * (sample_rate / 100);
}

void apu_init(struct apu **apu_out, uint32_t sample_rate, bool stereo)
{
	struct apu *apu = *apu_out = calloc(1, sizeof(struct apu));

	apu_set_channels(apu, CHANNEL_ALL);
	apu_set_stereo(apu, stereo);
	apu_set_sample_rate(apu, sample_rate);
	apu_dac_init();
}

void apu_destroy(struct apu **apu_out)
{
	if (!apu_out || !*apu_out) return;

	free(*apu_out);
	*apu_out = NULL;
}

void apu_reset(struct apu *apu, struct nes *nes, struct cpu *cpu, bool hard)
{
	memset(apu->p, 0, sizeof(struct pulse) * 2);
	memset(&apu->t, 0, sizeof(struct triangle));
	memset(&apu->n, 0, sizeof(struct noise));
	memset(&apu->d, 0, sizeof(struct dmc));

	apu->n.shift_register = 1;
	apu->p[0].len.enabled = apu->p[0].len.next_enabled = true;
	apu->p[1].len.enabled = apu->p[1].len.next_enabled = true;
	apu->n.len.enabled = apu->n.len.next_enabled = true;
	apu->cpu_cycle = 0;
	apu->t.pop = false;
	apu->frame_irq = false;

	apu_write(apu, nes, cpu, 0x4015, 0x00);

	if (hard) {
		apu->mode = apu->next_mode = false;
		apu->irq_disabled = false;
		apu->t.len.enabled = apu->t.len.next_enabled = true;
		apu_write(apu, nes, cpu, 0x4017, 0x00);
	}

	apu->delayed_reset = 0;
	apu->frame_counter = 4;
}
