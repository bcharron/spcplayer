/*
 * spcplayer.c - An SPC file player
 * Copyright (C) 2011 Benjamin Charron <bcharron@pobox.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * spcplayer.c - Benjamin Charron <bcharron@pobox.com>
 * Created  : Sat Sep  3 14:53:26 2011
 * Revision : $Id$
 */

/*
  Assembler notes:
  MOV X, A    ; Register X = A
  MOV Y, #$12 ; Register Y = 0x12 (#$xx == immediate)
  MOV Y, $12  ; Register Y = ram[0x12] ($xx == memory offset)
  MOV ($12)+Y, A ; Not sure! Maybe ram[ram[0x12] + Y] = A?
*/

#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <SDL.h>
#include <signal.h>

#include "opcodes.h"
#include "dsp_registers.h"
#include "ctl_registers.h"

#define SPC_HEADER_LEN 33
#define SPC_TAG_TYPE_OFFSET 0x23
#define SPC_VERSION_OFFSET 0x24
#define SPC_ID_TAG_OFFSET 0x2e
#define SPC_RAM_OFFSET 0x0100

#define SPC_DSP_REGISTERS 128
#define SPC_RAM_SIZE 65536
#define SPC_HEADER_MAGIC "SNES-SPC700 Sound File Data v0.30"
#define SPC_HAS_ID_TAG 26

#define SPC_TAG_SONG_TITLE_LEN 32
#define SPC_TAG_GAME_TITLE_LEN 32
#define SPC_TAG_DUMPER_NAME_LEN 32
#define SPC_TAG_COMMENTS_LEN 32

#define SPC_STACK_BASE 0x0100

#define NO_OPERAND 0

#define SPC_REG_CONTROL 0xF1
#define SPC_REG_TIMER0 0xFA
#define SPC_REG_TIMER1 0xFB
#define SPC_REG_TIMER2 0xFC

#define SPC_REG_COUNTER0 0xFD
#define SPC_REG_COUNTER1 0xFE
#define SPC_REG_COUNTER2 0xFF

// How many cycles before a timer's internal counter is incremented, based on
// 2.048 MHz clock
#define SPC_TIMER_CYCLES_8KHZ 250
#define SPC_TIMER_CYCLES_64KHZ 31

#define SPC_DSP_KON 0x4C
#define SPC_DSP_KOFF 0x5C
#define SPC_DSP_DIR 0x5D

// Per-voice registers
#define SPC_DSP_VxPITCHL 0x02
#define SPC_DSP_VxPITCHH 0x03
#define SPC_DSP_VxSCRN 0x04

enum error_values {
	SUCCESS = 0,
	FATAL_ERROR = 1
};

/* Passed to functions that may or not update flags */
#define DONT_ADJUST_FLAGS 0
#define ADJUST_FLAGS 1

enum trace_flags {
	TRACE_CPU_JUMPS = 0x01,
	TRACE_APU_VOICES = 0x02,
	TRACE_REGISTER_WRITES = 0x04,
	TRACE_REGISTER_READS = 0x08,
	TRACE_CPU_INSTRUCTIONS = 0x10,
	TRACE_COUNTERS = 0x20,
	TRACE_DSP_OPS = 0x40
};

#define TRACE_ALL (TRACE_CPU_JUMPS | TRACE_APU_VOICES | TRACE_REGISTER_WRITES | TRACE_REGISTER_READS | TRACE_CPU_INSTRUCTIONS | TRACE_COUNTERS | TRACE_DSP_OPS)

typedef union spc_flags_u {
	struct {
		int c : 1; // Carry
		int z : 1; // Zero
		int i : 1; // Interrupt Enable
		int h : 1; // Half-Carry
		int b : 1; // Break
		int p : 1; // Direct Page
		int v : 1; // Overflow
		int n : 1; // Negative
	} f;
	Uint8 val;
} spc_flags_t;

typedef struct brr_block_s {
	Sint16 samples[16];
	int filter;
	int loop_flag;
	int last_chunk;
	int loop_code;		// Addressing last_chunk + loop_flag as one 2-bit value.
} brr_block_t;

typedef struct spc_registers_s {
	Uint16 pc;
	Uint8 a;
	Uint8 x;
	Uint8 y;
	spc_flags_t psw;
	Uint8 sp;
	Uint8 reserved[2];
} spc_registers_t;

typedef struct spc_timers_s {
	unsigned long next_timer[3];	// Next cycle number for this timer to increase
	Uint8 timer[3];			// Increments by one every time next_timer == cycle
	Uint8 counter[3];		// Increments by one every time timer[x] == divisor[x]
	Uint8 divisor[3];		// How many times timer[x] must increment before we increment counter
} spc_timers_t;

typedef struct id_tag_s {
	char song_title[32 + 1];
	char game_title[32 + 1];
	char dumper[16 + 1];
	char comments[32 + 1];
	time_t date_dumped;
} id_tag_t;

typedef struct spc_file_s {
	char header[SPC_HEADER_LEN + 1];
	Uint8 junk[2];
	Uint8 tag_type;
	Uint8 version_minor;
	spc_registers_t registers;
	Uint8 ram[SPC_RAM_SIZE];
	Uint8 dsp_registers[128];
	Uint8 unused[64];
	Uint8 extra_ram[64];
	id_tag_t id_tag;
} spc_file_t;

/* Represents a voice */
typedef struct spc_voice_s {
	int enabled;		// 1 if enabled (KON), 0 otherwise
	Uint16 cur_addr;	// Address of current sample block
	int next_sample;	// Number of the next sample to play in the block
	int looping;		// Whether it's in looping mode
	brr_block_t *block;	// Current block
	int step;		// Current step, based on pitch
	int counter;		// Current counter, based on number of steps done for this block of 4 BRR samples so far
	Uint16 prev_brr_samples[4];	// Used for interpolation
} spc_voice_t;

typedef struct spc_state_s {
	spc_registers_t *regs;
	spc_timers_t timers;
	Uint8 *ram;
	Uint8 *dsp_registers;
	Uint8 current_dsp_register;
	unsigned long cycle;
	spc_voice_t voices[8];
	int trace;
	int profiling;
	int *profile_info;
} spc_state_t;

/* Gaussian Interpolation table - straight from no$sns specs */
int INTERP_TABLE[] = {
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x002, 0x002,
	0x002, 0x002, 0x003, 0x003, 0x003, 0x003, 0x003, 0x004, 0x004, 0x004, 0x004, 0x004, 0x005, 0x005, 0x005, 0x005,
	0x006, 0x006, 0x006, 0x006, 0x007, 0x007, 0x007, 0x008, 0x008, 0x008, 0x009, 0x009, 0x009, 0x00A, 0x00A, 0x00A,
	0x00B, 0x00B, 0x00B, 0x00C, 0x00C, 0x00D, 0x00D, 0x00E, 0x00E, 0x00F, 0x00F, 0x00F, 0x010, 0x010, 0x011, 0x011,
	0x012, 0x013, 0x013, 0x014, 0x014, 0x015, 0x015, 0x016, 0x017, 0x017, 0x018, 0x018, 0x019, 0x01A, 0x01B, 0x01B,
	0x01C, 0x01D, 0x01D, 0x01E, 0x01F, 0x020, 0x020, 0x021, 0x022, 0x023, 0x024, 0x024, 0x025, 0x026, 0x027, 0x028,
	0x029, 0x02A, 0x02B, 0x02C, 0x02D, 0x02E, 0x02F, 0x030, 0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037, 0x038,
	0x03A, 0x03B, 0x03C, 0x03D, 0x03E, 0x040, 0x041, 0x042, 0x043, 0x045, 0x046, 0x047, 0x049, 0x04A, 0x04C, 0x04D,
	0x04E, 0x050, 0x051, 0x053, 0x054, 0x056, 0x057, 0x059, 0x05A, 0x05C, 0x05E, 0x05F, 0x061, 0x063, 0x064, 0x066,
	0x068, 0x06A, 0x06B, 0x06D, 0x06F, 0x071, 0x073, 0x075, 0x076, 0x078, 0x07A, 0x07C, 0x07E, 0x080, 0x082, 0x084,
	0x086, 0x089, 0x08B, 0x08D, 0x08F, 0x091, 0x093, 0x096, 0x098, 0x09A, 0x09C, 0x09F, 0x0A1, 0x0A3, 0x0A6, 0x0A8,
	0x0AB, 0x0AD, 0x0AF, 0x0B2, 0x0B4, 0x0B7, 0x0BA, 0x0BC, 0x0BF, 0x0C1, 0x0C4, 0x0C7, 0x0C9, 0x0CC, 0x0CF, 0x0D2,
	0x0D4, 0x0D7, 0x0DA, 0x0DD, 0x0E0, 0x0E3, 0x0E6, 0x0E9, 0x0EC, 0x0EF, 0x0F2, 0x0F5, 0x0F8, 0x0FB, 0x0FE, 0x101,
	0x104, 0x107, 0x10B, 0x10E, 0x111, 0x114, 0x118, 0x11B, 0x11E, 0x122, 0x125, 0x129, 0x12C, 0x130, 0x133, 0x137,
	0x13A, 0x13E, 0x141, 0x145, 0x148, 0x14C, 0x150, 0x153, 0x157, 0x15B, 0x15F, 0x162, 0x166, 0x16A, 0x16E, 0x172,
	0x176, 0x17A, 0x17D, 0x181, 0x185, 0x189, 0x18D, 0x191, 0x195, 0x19A, 0x19E, 0x1A2, 0x1A6, 0x1AA, 0x1AE, 0x1B2,
	0x1B7, 0x1BB, 0x1BF, 0x1C3, 0x1C8, 0x1CC, 0x1D0, 0x1D5, 0x1D9, 0x1DD, 0x1E2, 0x1E6, 0x1EB, 0x1EF, 0x1F3, 0x1F8,
	0x1FC, 0x201, 0x205, 0x20A, 0x20F, 0x213, 0x218, 0x21C, 0x221, 0x226, 0x22A, 0x22F, 0x233, 0x238, 0x23D, 0x241,
	0x246, 0x24B, 0x250, 0x254, 0x259, 0x25E, 0x263, 0x267, 0x26C, 0x271, 0x276, 0x27B, 0x280, 0x284, 0x289, 0x28E,
	0x293, 0x298, 0x29D, 0x2A2, 0x2A6, 0x2AB, 0x2B0, 0x2B5, 0x2BA, 0x2BF, 0x2C4, 0x2C9, 0x2CE, 0x2D3, 0x2D8, 0x2DC,
	0x2E1, 0x2E6, 0x2EB, 0x2F0, 0x2F5, 0x2FA, 0x2FF, 0x304, 0x309, 0x30E, 0x313, 0x318, 0x31D, 0x322, 0x326, 0x32B,
	0x330, 0x335, 0x33A, 0x33F, 0x344, 0x349, 0x34E, 0x353, 0x357, 0x35C, 0x361, 0x366, 0x36B, 0x370, 0x374, 0x379,
	0x37E, 0x383, 0x388, 0x38C, 0x391, 0x396, 0x39B, 0x39F, 0x3A4, 0x3A9, 0x3AD, 0x3B2, 0x3B7, 0x3BB, 0x3C0, 0x3C5,
	0x3C9, 0x3CE, 0x3D2, 0x3D7, 0x3DC, 0x3E0, 0x3E5, 0x3E9, 0x3ED, 0x3F2, 0x3F6, 0x3FB, 0x3FF, 0x403, 0x408, 0x40C,
	0x410, 0x415, 0x419, 0x41D, 0x421, 0x425, 0x42A, 0x42E, 0x432, 0x436, 0x43A, 0x43E, 0x442, 0x446, 0x44A, 0x44E,
	0x452, 0x455, 0x459, 0x45D, 0x461, 0x465, 0x468, 0x46C, 0x470, 0x473, 0x477, 0x47A, 0x47E, 0x481, 0x485, 0x488,
	0x48C, 0x48F, 0x492, 0x496, 0x499, 0x49C, 0x49F, 0x4A2, 0x4A6, 0x4A9, 0x4AC, 0x4AF, 0x4B2, 0x4B5, 0x4B7, 0x4BA,
	0x4BD, 0x4C0, 0x4C3, 0x4C5, 0x4C8, 0x4CB, 0x4CD, 0x4D0, 0x4D2, 0x4D5, 0x4D7, 0x4D9, 0x4DC, 0x4DE, 0x4E0, 0x4E3,
	0x4E5, 0x4E7, 0x4E9, 0x4EB, 0x4ED, 0x4EF, 0x4F1, 0x4F3, 0x4F5, 0x4F6, 0x4F8, 0x4FA, 0x4FB, 0x4FD, 0x4FF, 0x500,
	0x502, 0x503, 0x504, 0x506, 0x507, 0x508, 0x50A, 0x50B, 0x50C, 0x50D, 0x50E, 0x50F, 0x510, 0x511, 0x511, 0x512,
	0x513, 0x514, 0x514, 0x515, 0x516, 0x516, 0x517, 0x517, 0x517, 0x518, 0x518, 0x518, 0x518, 0x518, 0x519, 0x519
};

/* Global variables */
volatile int g_do_break = 1;

int dump_instruction(Uint16 pc, Uint8 *ram);
void dump_registers(spc_registers_t *registers);
int execute_next(spc_state_t *state);
spc_file_t *read_spc_file(char *filename);
Uint16 get_direct_page_addr(spc_state_t *state, Uint16 addr);
Uint8 get_direct_page_byte(spc_state_t *state, Uint16 addr);
void adjust_flags(spc_state_t *state, Uint16 val);
Uint8 read_byte(spc_state_t *state, Uint16 addr);
Uint16 read_word(spc_state_t *state, Uint16 addr);
void write_byte(spc_state_t *state, Uint16 addr, Uint8 val);
void write_word(spc_state_t *state, Uint16 addr, Uint16 val);
void enable_timer(spc_state_t *state, int timer);
void clear_timer(spc_state_t *state, int timer);
Uint16 get_sample_addr(spc_state_t *state, int voice_nr, int loop);
brr_block_t *decode_brr_block(Uint8 *ptr);
void kon_voice(spc_state_t *state, int voice_nr);
void koff_voice(spc_state_t *state, int voice_nr);
void init_voice(spc_state_t *state, int voice_nr);
int get_voice_pitch(spc_state_t *state, int voice_nr);
Sint16 get_next_sample(spc_state_t *state, int voice_nr);

char *flags_str(spc_flags_t flags)
{
	static char buf[10];

	char *ptr = buf;

        *ptr++ = '[';
	*ptr++ = flags.f.n ? 'n' : ' ';
	*ptr++ = flags.f.v ? 'v' : ' ';
	*ptr++ = flags.f.p ? 'p' : ' ';
	*ptr++ = flags.f.b ? 'b' : ' ';
	*ptr++ = flags.f.h ? 'h' : ' ';
	*ptr++ = flags.f.i ? 'i' : ' ';
	*ptr++ = flags.f.z ? 'z' : ' ';
	*ptr++ = flags.f.c ? 'c' : ' ';
	*ptr++ = ']';
	*ptr++ = '\0';

	return(buf);
}

/* Convert from 16-bit little-endian */
// XXX: IMPLEMENT ME
uint16_t le16toh(uint16_t i) {
	return(i);
}

/* Make a 16-bit value out of two 8-bit ones */
uint16_t make16(uint8_t high, uint8_t low) {
	uint16_t offset = ((uint16_t) high << 8) | low;

	return(offset);
}

/* Get low byte of a 16-bit word */
uint8_t get_low(uint16_t word)
{
	uint8_t low = (word & 0x00FF);

	return(low);
}

/* Get high byte of a 16-bit word */
uint8_t get_high(uint16_t word)
{
	uint8_t high = (word & 0xFF00) >> 8;

	return(high);
}


// XXX: This is dumb. We need a table sorted by opcode rather than mnemonic..
opcode_t *get_opcode_by_value(Uint8 opcode) {
	opcode_t *ret = NULL;
	int x;

	for (x = 0; x < OPCODE_TABLE_LEN; x++) {
		if (OPCODE_TABLE[x].opcode == opcode) {
			ret = &OPCODE_TABLE[x];
			break;
		}
	}

	assert(ret != NULL);

	return(ret);
}

/*
	Dump a voice to file. If loop is defined, loops for 32k samples (~1 second of audio at 32kHz)
*/
void dump_voice(spc_state_t *state, int voice_nr, char *path) {
	FILE *f;
	int dealloc = 0;
	int pitch = 0x1000;	// XXX: Pitch is hardocded.
	int step = pitch;
	int counter = 0;
	int done = 0;
	int written_samples = 0;

	if (NULL == path) {
		path = malloc(1024);
		sprintf(path, "sample_%02d", voice_nr);
		dealloc = 1;
	}

	printf("Writing to %s\n", path);
	f = fopen(path, "w+");

	int addr = get_sample_addr(state, voice_nr, 0);

	pitch = get_voice_pitch(state, voice_nr);
	step = pitch;

	printf("Pitch: %d (%04X)\n", pitch, pitch);

	brr_block_t *block;

	counter = 0;
	do {
		block = decode_brr_block(&state->ram[addr]);
		addr += 9;

		counter = 0;

		// Go through samples 0..3, 4..7, 8..11, 12..15
		// for (int x = 0; x < 4; x++) {
		int x = 0;
			counter = counter % 65536;

			while (counter < (1 << 16)) {
				int brr_nr = ((counter & 0xF000) >> 12) & 0xF;
				int index = ((counter & 0x0FF0) >> 4);
				Sint16 sample = block->samples[x * 4 + brr_nr];

				assert(brr_nr <= 15);
				assert(index < sizeof(INTERP_TABLE) / sizeof(INTERP_TABLE[0]));


				// Uint16 out = (INTERP_TABLE[0x00 + index] * sample) >> 10;
				Uint16 out = (INTERP_TABLE[0x00 + index] * sample);

				fprintf(f, "%d\n", sample);

				written_samples++;

				printf("sample: %d   out: %d  brr_nr: %d   index: %d  counter: %d (%04X)\n", sample, out, brr_nr, index, counter, counter);

				counter += step;
			}
		// }

		/*
		for (int x = 0; x < 16; x++) {
			fprintf(f, "%d\n", block->samples[x]);
		}
		*/

		printf("last_chunk? %d\n", block->last_chunk);
		printf("loop? %d\n", block->loop_flag);

		if (block->last_chunk) {
			if (block->loop_flag) {
				printf("Starting loop.\n");
				addr = get_sample_addr(state, voice_nr, 1);
			} else {
				done = 1;
			}
		}
	} while(! done && written_samples < 32000 && addr < 65536);

	fclose(f);

	if (dealloc)
		free(path);
}

/* Read the value of a counter. Doing so resets the counter. */
Uint8 read_counter(spc_state_t *state, Uint16 addr) {
	Uint8 val;
	int counter_nr;

	assert(addr >= SPC_REG_COUNTER0 && addr <= SPC_REG_COUNTER2);

	counter_nr = addr - SPC_REG_COUNTER0;

	val = state->timers.counter[counter_nr];
	state->timers.counter[counter_nr] = 0;

	return(val);
}

/* Called when a register is being written to */
void dsp_register_write(spc_state_t *state, Uint8 reg, Uint8 val) {
	if (state->trace & (TRACE_REGISTER_WRITES|TRACE_DSP_OPS))
		printf("[DSP] Writing %02X into register %02X (%s)\n", val, reg, DSP_NAMES[reg % 127]);

	state->dsp_registers[reg] = val;

	switch(reg) {
		case SPC_DSP_KON:
		{
			for (int x = 0; x < 8; x++) {
				Uint8 bit = 1 << x;

				if ((val & bit) > 0) {
					if (state->trace & TRACE_APU_VOICES)
						printf("Enabling voice %d\n", x);

					kon_voice(state, x);
				}
			}
		}
		break;

		case SPC_DSP_KOFF:
		{
			for (int x = 0; x < 8; x++) {
				Uint8 bit = 1 << x;

				if ((val & bit) > 0) {
					if (state->trace & TRACE_APU_VOICES)
						printf("Disabling voice %d\n", x);

					koff_voice(state, x);
				}
			}
		}
		break;

		default:
			break;
	}
}

/* Handles a byte being written to $00F0-$00FF (registers) */
void register_write(spc_state_t *state, Uint16 addr, Uint8 val) {
	assert(addr >= 0xF0 && addr <= 0xFF);

	if (state->trace & TRACE_REGISTER_WRITES)
		printf("Register read $%04X [%s]\n", addr, CTL_REGISTER_NAMES[addr - 0xF0]);

	switch(addr) {
		case 0xF0:	// Test?
			state->ram[addr] = val;
			break;

		case 0xF1:	// Control, AKA SPCCON1, AKA CONTROL
			state->ram[addr] = val;

			// Start or stop a timer
			for (int timer = 0; timer < 3; timer++) {
				int bit = 0x01 << timer;

				// XXX: Handle the case where timer == 0x00, which is in fact 256.
				if (val & bit)
					enable_timer(state, timer);
				else
					clear_timer(state, timer);
			}

			// XXX: Handles bits 4-5 (PORT0-3)
			// XXX: Bit 7 appears to be related to the IPL ROM being ROM or RAM.
			break;

		case 0xF2:	// Register address port, AKA SPCDRGA, AKA DSPADDR
			state->current_dsp_register = val;
			if (val > 127) {
				fprintf(stderr, "Trying to access DSP register %d, but maximum is 127.\n", val);
				state->current_dsp_register = val % 127;
			}

			state->ram[addr] = val;
			break;

		case 0xF3:	// Register data port, AKA SPCDDAT, AKA DSPDATA
			dsp_register_write(state, state->current_dsp_register, val);
			state->ram[addr] = val;
			break;

		case 0xF4:	// I/O Ports, AKA CPUIO0
		case 0xF5:	// CPUIO1
		case 0xF6:	// CPUIO2
		case 0xF7:	// CPUIO3
			state->ram[addr] = val;
			break;

		case 0xF8:	// Unknown, AKA AUXIO4
		case 0xF9:	// AUXIO5
			state->ram[addr] = val;
			break;

		case 0xFA:	// Timers, AKA SPCTMLT, AKA T1DIV
		case 0xFB:	// T1DIV
		case 0xFC:	// T2DIV
			// XXX: Is the timer reset when the user changes the divider?
			state->ram[addr] = val;
			break;

		case 0xFD:	// Counters, AKA SPCTMCT, AKA TxOUT
		case 0xFE:	// T1OUT
		case 0xFF:	// T2OUT
			// I don't think these counters can be written to..
			// state->ram[addr] = val;
			break;

		default:
			fprintf(stderr, "register_write(%04X): HOW THE FUCK DID THIS HAPPEN??\n", addr);
			exit(1);
			break;
	}
}

Uint8 register_read(spc_state_t *state, Uint16 addr) {
	Uint8 val;

	assert(addr >= 0xF0 && addr <= 0xFF);

	if (state->trace & TRACE_REGISTER_READS)
		printf("$%04X: Register read $%04X [%s]\n", state->regs->pc, addr, CTL_REGISTER_NAMES[addr - 0xF0]);

	switch(addr) {
		case 0xF0:	// Test?
			val = state->ram[addr];
			break;

		case 0xF1:	// Control
			val = state->ram[addr];
			break;

		case 0xF2:	// Register stuff: Add
			val = state->ram[addr];
			break;

		case 0xF3:	// Register stuff: Data
			// val = state->ram[addr];
			val = state->dsp_registers[state->current_dsp_register];
			break;

		case 0xF4:	// I/O Ports
		case 0xF5:
		case 0xF6:
		case 0xF7:
			val = state->ram[addr];
			break;

		case 0xF8:	// Unknown
		case 0xF9:
			val = state->ram[addr];
			break;

		case 0xFA:	// Timers, AKA SPCTMLT, AKA T1DIV
		case 0xFB:	// T1DIV
		case 0xFC:	// T2DIV
			val = state->ram[addr];
			break;

		case 0xFD:	// Counters, AKA SPCTMCT, AKA TxOUT
		case 0xFE:	// T1OUT
		case 0xFF:	// T2OUT
			val = read_counter(state, addr);
			break;

		default:
			val = state->ram[addr];
			break;
	}

	return(val);
}

/* Write a byte to memory / registers */
void write_byte(spc_state_t *state, Uint16 addr, Uint8 val) {
	// Handle registers 0xF0-0xFF
	if ((addr & 0xFFF0) == 0x00F0) {
		// XXX: Handle register here.
		register_write(state, addr, val);
	} else
		state->ram[addr] = val;
}

void write_word(spc_state_t *state, Uint16 addr, Uint16 val) {
	// XXX: Don't know what the proper order is.
	Uint8 l = get_low(val);
	Uint8 h = get_high(val);

	write_byte(state, addr, l);
	write_byte(state, addr + 1, h);
}

/* Read a byte from memory / registers / whatever */
Uint8 read_byte(spc_state_t *state, Uint16 addr) {
	Uint8 val;

	// Handle registers 0xF0-0xFF
	if ((addr & 0xFFF0) == 0x00F0) {
		// XXX: Handle register here.
		val = register_read(state, addr);
	} else
		val = state->ram[addr];

	return(val);
}

/* Read a word (16-bit) from memory / registers / whatever */
Uint16 read_word(spc_state_t *state, Uint16 addr) {
	Uint16 ret;
	Uint8 l;
	Uint8 h;

	l = read_byte(state, addr);
	h = read_byte(state, addr + 1);

	ret = make16(h, l);

	return(ret);
}

/* Get the contents of DSP register X. Does not involve read_byte(). */
Uint8 get_dsp(spc_state_t *state, Uint8 reg) {
	Uint8 b;

	b = state->dsp_registers[reg];

	return(b);
}

/* Get the register X for DSP Voice Y */
Uint8 get_dsp_voice(spc_state_t *state, int voice_nr, Uint8 reg) {
	Uint8 addr;
	Uint8 result;

	addr = voice_nr * 0x10 + reg;

	result = get_dsp(state, reg);

	return(result);
}

/* Perform the branch if flag 'flag' is set */
int branch_if_flag(spc_state_t *state, int flag, Uint8 operand1) {
	int cycles;

	if (flag) {
		state->regs->pc += (Sint8) operand1 + 2;

		if (state->trace & TRACE_CPU_JUMPS)
			printf("Jumping to 0x%04X\n", state->regs->pc);

		cycles = 6;
	} else {
		state->regs->pc += 2;
		cycles = 4;
	}

	return(cycles);
}

int branch_if_flag_clear(spc_state_t *state, int flag, Uint8 operand1) {
	int ret;

	ret = branch_if_flag(state, ! flag, operand1);

	return(ret);
}

int branch_if_flag_set(spc_state_t *state, int flag, Uint8 operand1) {
	int ret;

	ret = branch_if_flag(state, flag, operand1);

	return(ret);
}

/*
int do_bcc(spc_state_t *state, Uint8 operand1) {
	int cycles = 2;

	if (! state->regs->psw.f.c) {
		state->regs->pc += (Sint8) operand1 + 2;
		printf("Jumping to 0x%04X\n", state->regs->pc);
	} else {
		state->regs->pc += 2;
		cycles = 4;
	}

	return(cycles);
}
*/

/*
int do_beq(spc_state_t *state, Uint8 operand1)
{
	int cycles = 2;

	if (state->regs->psw.f.z) {
		state->regs->pc += (Sint8) operand1 + 2;
		printf("Jumping to 0x%04X\n", state->regs->pc);
	} else {
		state->regs->pc += 2;
		cycles = 4;
	}

	return(cycles);
}
*/

/* Jump if bit 'bit' of the addr is clear */
int do_bbc(spc_state_t *state, int bit, Uint16 src_addr, Uint8 rel) {
	int cycles;
	Uint8 val;
	Uint8 test;
	
	test = 1 << bit;

	val = read_byte(state, src_addr);

	// printf("DO_BBC(%d): Jump if %02X (%04X) & %02X == 0\n", bit, val, src_addr, test);

	if (val & test) {
		cycles = 5;
		state->regs->pc += 3;
	} else {
		state->regs->pc += (Sint8) rel + 3;

		if (state->trace & TRACE_CPU_JUMPS)
			printf("Jumping to 0x%04X\n", state->regs->pc);

		cycles = 7;
	}

	return(cycles);
}

/* Jump if bit 'bit' of the addr is set */
int do_bbs(spc_state_t *state, int bit, Uint16 src_addr, Uint8 rel) {
	int cycles;
	Uint8 test;
	
	test = 1 << bit;

	if (state->ram[src_addr] & test) {
		state->regs->pc += (Sint8) rel + 3;

		if (state->trace & TRACE_CPU_JUMPS)
			printf("Jumping to 0x%04X\n", state->regs->pc);

		cycles = 7;
	} else {
		state->regs->pc += 3;
		cycles = 5;
	}

	return(cycles);
}

void instr_or(spc_state_t *state, Uint8 *operand1, Uint8 operand2)
{
	printf("OR %02X, %02X\n", *operand1, operand2);
	*operand1 = *operand1 | operand2;

	state->regs->psw.f.n = (*operand1 & 0x80) > 0;
	state->regs->psw.f.z = (*operand1 == 0);
}

Uint8 do_rol(spc_state_t *state, Uint8 val) {
	Uint8 new_carry = (val & 0x80) > 0;

	val <<= 1;
	val |= state->regs->psw.f.c;

	state->regs->psw.f.c = new_carry;

	adjust_flags(state, val);

	return(val);
}

Uint8 do_pop(spc_state_t *state) {
	Uint8 ret;
	Uint16 stack_addr;

	state->regs->sp++;
	stack_addr = SPC_STACK_BASE + state->regs->sp;

	ret = state->ram[stack_addr];

	return(ret);
}

void do_push(spc_state_t *state, Uint8 val) {
	Uint16 stack_addr;

	stack_addr = SPC_STACK_BASE + state->regs->sp;

	state->ram[stack_addr] = val;
	state->regs->sp--;
}

void do_ret(spc_state_t *state) {
	Uint16 ret_addr;
	Uint8 h, l;

	l = do_pop(state);
	h = do_pop(state);

	ret_addr = make16(h, l);

	// printf("Popped address %04X\n", ret_addr);

	state->regs->pc = ret_addr;
	if (state->trace & TRACE_CPU_JUMPS)
		printf("Returning to $%04X\n", state->regs->pc);
}

void do_call(spc_state_t *state, Uint8 operand1, Uint8 operand2) {
	Uint16 ret_addr;
	Uint16 dest_addr;

	ret_addr = state->regs->pc + 3;

	if (state->trace & TRACE_CPU_JUMPS)
		printf("Pushing return address $%04X on the stack\n", ret_addr);

	do_push(state, get_high(ret_addr));
	do_push(state, get_low(ret_addr));

	dest_addr = make16(operand2, operand1);
	state->regs->pc = dest_addr;

	if (state->trace & TRACE_CPU_JUMPS)
		printf("Jumping to $%04X\n", state->regs->pc);
}

/* Update the flags based on (operand1 - operand2) */
void do_cmp(spc_state_t *state, Uint8 operand1, Uint8 operand2) {
	Sint16 sResult;
	Uint16 result;
	Uint8 temp_result;

	result = operand1 - operand2;
	sResult = (Sint8) operand1 - (Sint8) operand2;
	state->regs->psw.f.c = !(result > 0xFF);

	// XXX: According to spc700.txt, the V flag is not updated by CMP.
	state->regs->psw.f.v = (sResult < -128 || sResult > 127);

	temp_result = result & 0x00FF;

	adjust_flags(state, temp_result);
}

Uint8 do_adc(spc_state_t *state, Uint8 dst, Uint8 operand) {
	Uint16 result;
	Sint16 sResult;
	Uint8 ret;

	sResult = (Sint8) dst + (Sint8) operand + state->regs->psw.f.c;
	result = dst + operand + state->regs->psw.f.c;
	ret = (result & 0x00FF);
	state->regs->psw.f.c = (result > 0xFF);

	// One reference says "result == 0", but I think it would
	// makes more sense if "A == 0", since for other operations is
	// essentially checks if <reg> is zero.
	state->regs->psw.f.v = (sResult < -128 || sResult > 127);
	state->regs->psw.f.z = (ret == 0);  // 65C02 mode. In 6502, 'result' is tested.
	state->regs->psw.f.n = ((ret & 0x80) != 0);

	return(ret);
}

Uint8 do_sbc(spc_state_t *state, Uint8 dst, Uint8 operand) {
	Uint16  result;
	Sint16 sResult;
	Uint8 ret;

	result = dst - operand - (! state->regs->psw.f.c);
	sResult = (Sint8) dst - (Sint8) operand - (! state->regs->psw.f.c);
	ret = result & 0x00FF;

	state->regs->psw.f.c = !(result > 0xFF);
	state->regs->psw.f.n = ((ret & 0x80) != 0);
	state->regs->psw.f.v = (sResult < -128 || sResult > 127);
	state->regs->psw.f.z = (ret == 0);

	return(ret);
}

Uint16 do_sub_ya(spc_state_t *state, Uint16 val) {
	unsigned int result;
	Uint16 ya;
	Uint16 ret;
	int sResult;

	ya = make16(state->regs->y, state->regs->a);

	result = ya - val;
	// printf("Result: %08x\n", result);
	sResult = ya - val;
	ret = (Uint16) (result & 0xFFFF);

	state->regs->psw.f.c = !(result > 0xFFFF);
	state->regs->psw.f.n = ((ret & 0x80) != 0);
	state->regs->psw.f.v = (sResult < -32768 || sResult > 32767);
	state->regs->psw.f.z = (ret == 0);

	state->regs->y = get_high(ret);
	state->regs->a = get_low(ret);

	return(ret);
}

Uint16 do_add_ya(spc_state_t *state, Uint16 val) {
	unsigned int result;
	Uint16 ya;
	Uint16 ret;
	int sResult;

	ya = make16(state->regs->y, state->regs->a);

	result = ya + val;
	// printf("Result: %08x\n", result);
	sResult = ya + val;
	ret = (Uint16) (result & 0xFFFF);

	state->regs->psw.f.c = (result > 0xFFFF);
	state->regs->psw.f.n = ((ret & 0x80) != 0);
	state->regs->psw.f.v = (sResult < -32768 || sResult > 32767);
	state->regs->psw.f.z = (ret == 0);

	state->regs->y = get_high(ret);
	state->regs->a = get_low(ret);

	return(ret);
}

/* Flag 'P' can change if $00 means $0000 or $0100 */
Uint16 get_direct_page_addr(spc_state_t *state, Uint16 addr) {
	Uint16 base = 0x0000;
	Uint16 ret;

	if (state->regs->psw.f.p)
		base = 0x0100;

	ret = addr + base;

	return(ret);
}

Uint8 get_direct_page_byte(spc_state_t *state, Uint16 addr) {
	Uint8 ret;

	Uint16 real_addr = get_direct_page_addr(state, addr);
	ret = read_byte(state, real_addr);

	return(ret);
}

/* Adjust Zero and Negative flag based on value in 'val' */
void adjust_flags(spc_state_t *state, Uint16 val) {
	state->regs->psw.f.n = (val & 0x80) > 0;
	state->regs->psw.f.z = (val == 0);
}

int TIMER_CYCLES[3] = { SPC_TIMER_CYCLES_8KHZ, SPC_TIMER_CYCLES_8KHZ, SPC_TIMER_CYCLES_64KHZ };

/* Go through Timers 0-2. If enough cycles have elapsed, the counter 'ticks' */
void update_counters(spc_state_t *state) {
	// 2.048 MHz / 8kHz = 250
	// 2.048 MHz / 64kHz = 31

	// XXX: Most likely want a single "next_counter" (min of all next_timer
	// counters) to avoid going through all this every single tick.
	for (int timer = 0; timer < 3; timer++) {
		int bit = 0x01 << timer;

		if ((state->ram[SPC_REG_CONTROL] & bit) && (state->cycle >= state->timers.next_timer[timer])) {
			state->timers.next_timer[timer] = state->cycle + TIMER_CYCLES[timer];

			state->timers.timer[timer]++;

			/* We only reach this part when timer increments, and
			 * counter is initialized to 0x00, so it should be safe
			 * for the 0x00 edge case (divisor is 256)
			 */
			if (state->timers.timer[timer] == state->timers.divisor[timer]) {
				// This is a 16 bit counter.
				state->timers.counter[timer] = (state->timers.counter[timer] + 1) % 16;

				if (state->trace & TRACE_COUNTERS)
					printf("TIMER %d HIT (divisor is %d)\n", timer, state->timers.divisor[timer]);
			}
		}
	}
}

/* Disabling a timer resets its counter and reloads the diviser */
void clear_timer(spc_state_t *state, int timer) {
	state->timers.next_timer[timer] = 0;
	state->timers.counter[timer] = 0;
	state->timers.timer[timer] = 0;
	state->timers.divisor[timer] = state->ram[SPC_REG_TIMER0 + timer];
}

/* Enabling a timer through 0xF1 (CONTROL) */
void enable_timer(spc_state_t *state, int timer) {
	state->timers.next_timer[timer] = state->cycle + TIMER_CYCLES[timer];
	state->timers.counter[timer] = 0;	// Internal counter, increased every time timer[x] == divisor[x]
	state->timers.timer[timer] = 0;		// Internal counter, increased every clock

	// Reload the divisor
	state->timers.divisor[timer] = state->ram[SPC_REG_TIMER0 + timer];
	// state->timers.divisor[timer] = value;
}

int execute_instruction(spc_state_t *state, Uint16 addr) {
	Uint16 dp_addr;
	Uint16 abs_addr;
	Uint8 opcode;
	Uint8 operand1;
	Uint8 operand2;
	Uint8 val;
	opcode_t *opcode_ptr;
	int cycles = 0;
	int pc_adjusted = 0;

	// dump_registers(state->regs);
	// dump_instruction(addr, state->ram);

	opcode = state->ram[addr];
	operand1 = state->ram[addr + 1];
	operand2 = state->ram[addr + 2];

	opcode_ptr = get_opcode_by_value(opcode);

	switch(opcode) {
		case 0x00: // NOP
			cycles = 1;
			break;

		case 0x03: // BBS0 $00xx, $yy
			dp_addr = get_direct_page_addr(state, operand1);
			cycles = do_bbs(state, 0, dp_addr, operand2);
			pc_adjusted = 1;
			break;

		case 0x04: // ORZ A, $dp
			val = get_direct_page_byte(state, operand1);
			state->regs->a |= val;
			adjust_flags(state, state->regs->a);
			cycles = 3;
			break;

		case 0x08: // OR A, #$xx
			state->regs->a |= operand1;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x0B: // ASL $xx
			dp_addr = get_direct_page_addr(state, operand1);
			state->regs->psw.f.c = (state->ram[dp_addr] & 0x80) > 0;
			state->ram[dp_addr] <<= 1;
			adjust_flags(state, state->ram[dp_addr]);
			cycles = 4;
			break;

		case 0x0E: //  TSET1 $xx
			abs_addr = make16(operand2, operand1);
			val = state->ram[abs_addr] - state->regs->a;
			state->ram[abs_addr] |= state->regs->a;
			adjust_flags(state, val);
			cycles = 6;
			break;

		case 0x10: // BPL
			cycles = branch_if_flag_clear(state, state->regs->psw.f.n, operand1);
			pc_adjusted = 1;
			break;

		case 0x13: // BBC0 $dp, $yy
			// XXX: BBC0 is 0x13?
			dp_addr = get_direct_page_addr(state, operand1);
			cycles = do_bbc(state, 0, dp_addr, operand2);
			pc_adjusted = 1;
			break;

		case 0x1C: // ASL A
			state->regs->psw.f.c = (state->regs->a & 0x80) > 0;
			state->regs->a = state->regs->a << 1;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x1E: // CMP X, $xxyy
		{
			abs_addr = make16(operand2, operand1);
			val = read_byte(state, abs_addr);
			do_cmp(state, state->regs->x, val);
			cycles = 4;
		}
		break;

		case 0x24: // ANDZ A, $xx
			val = get_direct_page_byte(state, operand1);
			state->regs->a &= val;
			cycles = 2;
			break;

		case 0x2B: // ROLZ $xx
			dp_addr = get_direct_page_addr(state, operand1);
			val = read_byte(state, dp_addr);
			val = do_rol(state, val);
			write_byte(state, dp_addr, val);
			cycles = 4;
			break;

		case 0x2D: // PUSH A
			do_push(state, state->regs->a);
			cycles = 4;
			break;

		case 0x2F: // BRA xx
			branch_if_flag(state, 1, operand1);
			pc_adjusted = 1;
			cycles = 4;
			break;	

		case 0x30: // BMI
			cycles = branch_if_flag_set(state, state->regs->psw.f.n, operand1);
			pc_adjusted = 1;
			break;

		case 0x33: // BBC1 $00xx, $yy
			dp_addr = get_direct_page_addr(state, operand1);
			cycles = do_bbc(state, 1, dp_addr, operand2);
			pc_adjusted = 1;
			break;

		case 0x3D: // INC X
			state->regs->x++;
			adjust_flags(state, state->regs->x);
			cycles = 2;
			break;

		case 0x3E: // CMP X, $xx
		{
			Uint8 val1 = get_direct_page_byte(state, operand1);

			do_cmp(state, state->regs->x, val1);
			cycles = 6;
		}
		break;

		case 0x3F: // CALL $xxyy
			do_call(state, operand1, operand2);
			cycles = 8;
			pc_adjusted = 1;
			break;

		case 0x44: // EORZ A, $xx
			val = get_direct_page_addr(state, operand1);
			state->regs->a ^= val;
			adjust_flags(state, state->regs->a);
			cycles = 3;
			break;

		case 0x48: // EOR A, $#imm
			state->regs->a ^= operand1;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x4B: // LSRZ $xx
			val = get_direct_page_addr(state, operand1);
			state->regs->psw.f.c = val & 0x01;
			val >>= 1;
			adjust_flags(state, val);
			cycles = 2;
			break;

		case 0x4D: // PUSH X
			do_push(state, state->regs->x);
			cycles = 4;
			break;

		case 0x50: // BVC
			cycles = branch_if_flag_clear(state, state->regs->psw.f.v, operand1);
			pc_adjusted = 1;
			break;

		case 0x5C: // LSR A
			state->regs->psw.f.c = state->regs->a & 0x01;
			state->regs->a = state->regs->a >> 1;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x5D: // MOV X, A
			state->regs->x = state->regs->a;
			adjust_flags(state, state->regs->x);
			cycles = 2;
			break;

		case 0x5F: // JMP $xxxx
		{
			Uint16 operand = make16(operand2, operand1);
			state->regs->pc = operand;
			pc_adjusted = 1;
			cycles = 3;

			if (state->trace & TRACE_CPU_JUMPS)
				printf("JMP to %04X\n", operand);
		}
		break;

		case 0x60: // CLRC
			state->regs->psw.f.c = 0;
			cycles = 2;
			break;

		case 0x65: // CMP A, $xxyy
		{
			abs_addr = make16(operand2, operand1);
			val = read_byte(state, abs_addr);
			do_cmp(state, state->regs->a, val);
			cycles = 4;
		}
		break;
		
		case 0x68: //  CMP A, #$xx
			do_cmp(state, state->regs->a, operand1);
			cycles = 2;
			break;

		case 0x69: // CMP $xx, $yy
		{
			Uint8 val1;
			Uint8 val2;

			val1 = get_direct_page_byte(state, operand1);
			val2 = get_direct_page_byte(state, operand2);

			do_cmp(state, val2, val1);
			cycles = 6;
		}
		break;

		case 0x6D: // PUSH Y
			do_push(state, state->regs->y);
			cycles = 4;
			break;

		case 0x6F: // RET
			do_ret(state);
			cycles = 5;
			pc_adjusted = 1;
			break;

		case 0x70: // BVS
			cycles = branch_if_flag_set(state, state->regs->psw.f.v, operand1);
			pc_adjusted = 1;
			break;

		case 0x7A: // ADDW YA, $xx
		{
			Uint8 l = get_direct_page_byte(state, operand1);
			Uint8 h = get_direct_page_byte(state, operand1 + 1);

			Uint16 operand = make16(h, l);

			do_add_ya(state, operand);
			cycles = 5;
		}
		break;

		case 0x75: // CMP A, $xxyy + X
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->x;

			do_cmp(state, state->regs->a, read_byte(state, abs_addr));
			cycles = 5;
			break;

		case 0x7C: // ROR A
			val  = state->regs->a & 0x01;
			state->regs->a >>= 1;
			state->regs->a |= ((Uint8) state->regs->psw.f.c << 7);
			state->regs->psw.f.c = val;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x7D: // MOV A, X
			state->regs->a = state->regs->x;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x7E: // CMP Y, $dp
			val = get_direct_page_byte(state, operand1);
			do_cmp(state, state->regs->a, val);
			cycles = 3;
			break;

		case 0x80: // SETC
			state->regs->psw.f.c = 1;
			cycles = 2;
			break;

		case 0x84: // ADC A, $dp
			dp_addr = get_direct_page_addr(state, operand1);
			val = read_byte(state, dp_addr);
			state->regs->a = do_adc(state, state->regs->a, val);
			cycles = 3;
			break;

		case 0x8D: // MOV Y,#$xx
			state->regs->y = operand1;
			adjust_flags(state, state->regs->y);
			cycles = 2;
			break;

		/*
		XXX: Need to get the exact binary output for PSW.
		case 0x8E: // POP PSW
			state->regs->psw = do_pop(state);
			cycles = 4;
			break;
		*/

		case 0x8F: // MOV $dp, #$xx
			dp_addr = get_direct_page_addr(state, operand2);
			write_byte(state, dp_addr, operand1);
			cycles = 5;
			break;

		case 0x90: // BCC
			cycles = branch_if_flag_clear(state, state->regs->psw.f.c, operand1);
			pc_adjusted = 1;
			break;

		case 0x95: // ADC A, $xxxx + X
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->x;

			val = read_byte(state, abs_addr);
			
			state->regs->a = do_adc(state, state->regs->a, val);
			cycles = 5;
			break;

		case 0x96: // ADC A, $xxxx + Y
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->y;

			val = read_byte(state, abs_addr);
			
			state->regs->a = do_adc(state, state->regs->a, val);
			cycles = 5;
			break;

		case 0x9A: // SUBW YA, $xx
		{
			Uint8 l = get_direct_page_byte(state, operand1);
			Uint8 h = get_direct_page_byte(state, operand1 + 1);

			Uint16 operand = make16(h, l);

			do_sub_ya(state, operand);
			cycles = 5;
		}
		break;

		case 0x9B: // DEC $dp+X
		{
			dp_addr = get_direct_page_addr(state, operand1);
			dp_addr += state->regs->x;

			val = read_byte(state, dp_addr);

			val--;

			adjust_flags(state, val);

			write_byte(state, dp_addr, val);

			cycles = 5;
		}
		break;

		case 0x9E: // DIV YA, X
		{
			// XXX: Not sure at all about this one.
			Uint16 ya = make16(state->regs->y, state->regs->a);

			state->regs->a = ya / state->regs->x;
			state->regs->y = ya % state->regs->x;

			// XXX: Update flags.. from A?
			adjust_flags(state, state->regs->a);

			// XXX: How to update the V and H flags?

			cycles = 12;
		}
		break;

		case 0x9F: // XCN A
			state->regs->a = ((state->regs->a << 4) & 0xF0) | (state->regs->a >> 4);
			adjust_flags(state, state->regs->a);
			cycles = 5;
			break;

		case 0xA8: // SBC A, $#imm
			state->regs->a = do_sbc(state, state->regs->a, operand1);
			cycles = 2;
			break;

		case 0xAB: // INC $xx
			dp_addr = get_direct_page_addr(state, operand1);
			state->ram[dp_addr]++;
			adjust_flags(state, state->ram[dp_addr]);
			cycles = 4;
			break;

		case 0xAD: // CMP Y,#$xx
			do_cmp(state, state->regs->y, operand1);
			cycles = 2;
			break;

		case 0xAE: // POP A
			state->regs->a = do_pop(state);
			cycles = 4;
			break;

		case 0xB0: // BCS $xx
			cycles = branch_if_flag_set(state, state->regs->psw.f.c, operand1);
			pc_adjusted = 1;
			break;

		case 0xB6: // SBC A, $xxxx + Y
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->y;

			val = read_byte(state, abs_addr);
			
			state->regs->a = do_sbc(state, state->regs->a, val);
			// state->regs->a = state->regs->a - val - state->regs->psw.f.c;
			cycles = 5;
			break;

		case 0xBA: // MOVW YA, $dp
			dp_addr = get_direct_page_addr(state, operand1);
			state->regs->a = read_byte(state, dp_addr);
			state->regs->y = read_byte(state, dp_addr + 1);

			// Manually adjusting flags because adjust_flags()
			// doesn't know how to handle "YA".
			if (state->regs->y == 0 && state->regs->a == 0)
				state->regs->psw.f.z = 1;

			if ((state->regs->y & 0x80) > 0)
				state->regs->psw.f.n = 1;

			cycles = 4;
			break;

		case 0xBB: // INC $dp+X
		{
			dp_addr = get_direct_page_addr(state, operand1);
			dp_addr += state->regs->x;

			val = read_byte(state, dp_addr);

			val++;

			adjust_flags(state, val);

			write_byte(state, dp_addr, val);

			cycles = 5;
		}

		case 0xC4: // MOVZ $xx, A
			dp_addr = get_direct_page_addr(state, operand1);
			write_byte(state, dp_addr, state->regs->a);
			cycles = 4;
			break;

		case 0xC5: // MOV $xxxx, A
			abs_addr = make16(operand2, operand1);
			write_byte(state, abs_addr, state->regs->a);
			cycles = 5;
			break;

		case 0xC8: // CMP X, #$xx
			do_cmp(state, state->regs->x, operand1);
			cycles = 2;
			break;

		case 0xCB: // MOV $xx, Y
			dp_addr = get_direct_page_addr(state, operand1);
			write_byte(state, dp_addr, state->regs->y);
			cycles = 4;
			break;

		case 0xCC: // MOV $xxxx, Y
			abs_addr = make16(operand2, operand1);
			write_byte(state, abs_addr, state->regs->y);
			cycles = 5;
			break;

		case 0xCD: // MOV X, #$xx
			state->regs->x = operand1;
			adjust_flags(state, state->regs->x);
			cycles = 2;
			break;

		case 0xCE: // POP X
			state->regs->x = do_pop(state);
			cycles = 4;
			break;

		case 0xCF: // MUL YA
		{
			Uint16 result = state->regs->y * state->regs->a;

			// XXX: Is it the other way around?
			state->regs->y = get_high(result);
			state->regs->a = get_low(result);
			cycles = 9;
		}
		break;

		case 0xD0: // BNE $xx
			cycles = branch_if_flag_clear(state, state->regs->psw.f.z, operand1);
			pc_adjusted = 1;
			break;

		case 0xD4: // MOVZ $xx + X, A
			dp_addr = get_direct_page_addr(state, operand1);
			dp_addr += state->regs->x;
			write_byte(state, dp_addr, state->regs->a);
			cycles = 5;
			break;

		case 0xD5: // MOV $xxxx + X, A
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->x;
			write_byte(state, abs_addr, state->regs->a);
			cycles = 6;
			break;

		case 0xD8: // MOV $xx, X
			dp_addr = get_direct_page_addr(state, operand1);
			write_byte(state, dp_addr, state->regs->x);
			cycles = 4;
			break;

		case 0xDA: // MOVW $xx, YA
			dp_addr = get_direct_page_addr(state, operand1);
			// XXX: Not sure what the order is!! Maybe A gets written first??
			write_byte(state, dp_addr + 1, state->regs->y);
			write_byte(state, dp_addr, state->regs->a);
			cycles = 4;	// XXX: One source says 4, another 5..
			break;

		case 0xDD: // MOV A, Y
			state->regs->a = state->regs->y;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0xDE: // CBNE $xx + X, $r
			// One of the few instructions where operand2 is 'r'
			dp_addr = get_direct_page_addr(state, operand1);
			dp_addr += state->regs->x;
			val = read_byte(state, dp_addr);
			Uint8 old_flags = state->regs->psw.val;
			do_cmp(state, state->regs->a, val);

			if (state->regs->psw.f.z) {
				cycles = 6;
				state->regs->pc += 3;
			} else {
				state->regs->pc += (Sint8) operand2 + 3;
				cycles = 8;

				if (state->trace & TRACE_CPU_JUMPS)
					printf("Jumping to 0x%04X\n", state->regs->pc);
			}

			/*
			// XXX: This will only increment PC by 2, it should be incremented by 3.
			cycles = branch_if_flag_clear(state, state->regs->psw.f.z, operand2);

			// Account for missing +1 in branch_if_clear.
			state->regs->pc++;
			*/

			// Restore the flags because CBNE should not modify them.
			state->regs->psw.val = old_flags;
			pc_adjusted = 1;
			cycles += 2;
			break;

		case 0xE2: // SET7 $xx
			dp_addr = get_direct_page_addr(state, operand1);
			state->ram[dp_addr] |= 0x80;
			cycles += 4;
			break;

		case 0xE3: // BBS7 $00xx, $yy
			dp_addr = get_direct_page_addr(state, operand1);
			cycles = do_bbs(state, 7, dp_addr, operand2);
			pc_adjusted = 1;
			break;

		case 0xE4: // MOVZ A, $xx
			val = get_direct_page_byte(state, operand1);
			state->regs->a = val;
			adjust_flags(state, state->regs->a);
			cycles = 3;
			break;

		case 0xE5: // MOV A, $xxxx
			abs_addr = make16(operand2, operand1);
			state->regs->a = read_byte(state, abs_addr);
			adjust_flags(state, state->regs->a);
			cycles = 4;
			break;

		case 0xE6: // MOV A, (X)
			// XXX: Not sure if direct-page flag applies to this operation.
			val = get_direct_page_byte(state, state->regs->x);
			state->regs->a = val;
			adjust_flags(state, state->regs->a);
			cycles = 3;
			break;

		case 0xE7: // MOV A,[$dp+X]
		{
			dp_addr = get_direct_page_addr(state, operand1);
			dp_addr += state->regs->x;

			Uint8 l = read_byte(state, dp_addr);
			Uint8 h = read_byte(state, dp_addr + 1);

			abs_addr = make16(h, l);

			state->regs->a = read_byte(state, abs_addr);

			adjust_flags(state, state->regs->a);

			cycles = 6;
		}
		break;

		case 0xE8: // MOV A, #$xx
			state->regs->a = operand1;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0xE9: // MOV X, $xxxx
			abs_addr = make16(operand2, operand1);
			state->regs->x = read_byte(state, abs_addr);
			adjust_flags(state, state->regs->x);
			cycles = 4;
			break;

		case 0xEB: // MOV Y, $xx
			val = get_direct_page_byte(state, operand1);
			state->regs->y = val;
			adjust_flags(state, state->regs->y);
			cycles = 3;
			break;

		case 0xEC: // MOV Y, $xxxx
			abs_addr = make16(operand2, operand1);
			state->regs->y = read_byte(state, abs_addr);
			adjust_flags(state, state->regs->y);
			cycles = 4;

		case 0xED: // NOTC
			state->regs->psw.f.c = ! state->regs->psw.f.c;
			cycles = 3;
			break;

		case 0xEE: // POP Y
			state->regs->y = do_pop(state);
			cycles = 4;
			break;

		case 0xF0: // BEQ
			cycles = branch_if_flag_set(state, state->regs->psw.f.z, operand1);
			pc_adjusted = 1;
			break;

		case 0xF2: // CLR7 $11
			dp_addr = get_direct_page_addr(state, operand1);
			val = read_byte(state, dp_addr);
			val &= (~ 0x80);
			write_byte(state, dp_addr, val);
			cycles = 4;
			break;

		case 0xF3: // BBC7 $dp, $r
			dp_addr = get_direct_page_addr(state, operand1);
			cycles = do_bbc(state, 7, dp_addr, operand2);
			pc_adjusted = 1;
			break;

		case 0xF4: // MOVZ A, $xx + X
			dp_addr = get_direct_page_addr(state, operand1);
			dp_addr += state->regs->x;
			state->regs->a = read_byte(state, dp_addr);
			adjust_flags(state, state->regs->a);
			cycles = 4;
			break;

		case 0xF5: // MOV A, $xxxx + X
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->x;
			state->regs->a = read_byte(state, abs_addr);
			adjust_flags(state, state->regs->a);
			cycles = 5;
			break;

		case 0xF6: // MOV A, $xxxx + Y
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->y;
			state->regs->a = read_byte(state, abs_addr);
			adjust_flags(state, state->regs->a);
			cycles = 5;
			break;

		case 0xFB: // MOVZ Y, $xx + X
			dp_addr = get_direct_page_addr(state, operand1);
			dp_addr += state->regs->x;
			state->regs->y = read_byte(state, dp_addr);
			adjust_flags(state, state->regs->y);
			cycles = 4;
			break;

		case 0xFC: // INC Y
			state->regs->y++;
			adjust_flags(state, state->regs->y);
			cycles = 2;
			break;

		case 0xFD: // MOV Y, A
			state->regs->y = state->regs->a;
			adjust_flags(state, state->regs->y);
			cycles = 2;
			break;

		case 0xFE: // DBNZ Y, $xx
			state->regs->y--;
			adjust_flags(state, state->regs->y);
			cycles = branch_if_flag_clear(state, state->regs->psw.f.z, operand1);
			pc_adjusted = 1;
			break;

		default:
			fprintf(stderr, "Instruction #$%02X at $%04X not implemented\n", opcode, addr);
			exit(1);
			break;
	}

	/* Increment PC if not a branch */
	if (! pc_adjusted) {
		state->regs->pc += opcode_ptr->len;
	}

	assert(cycles > 0);

	state->cycle += cycles;

	return(0);
}

int execute_next(spc_state_t *state) {

	if (state->profiling) {
		state->profile_info[state->regs->pc]++;
	}

	execute_instruction(state, state->regs->pc);


	return(0);
}

void dump_registers(spc_registers_t *registers)
{
	printf("== Registers ==\n");
	printf("PC : %u (0x%04X)\n", registers->pc, registers->pc);
	printf("A  : %u (0x%02X)\n", registers->a, registers->a);
	printf("X  : %u (0x%02X)\n", registers->x, registers->x);
	printf("Y  : %u (0x%02X)\n", registers->y, registers->y);
	printf("PSW: 0x%02X %s\n", registers->psw.val, flags_str(registers->psw));
	printf("SP : %u (0x%02X)\n", registers->sp, registers->sp);
}

// Dump and instruction and return its size in bytes
int dump_instruction(Uint16 pc, Uint8 *ram)
{
	Uint8 opcode = ram[pc];
	opcode_t *op = NULL;
	int x;

	printf("%04X  ", pc);

	for (x = 0; x < OPCODE_TABLE_LEN; x++) {
		if (OPCODE_TABLE[x].opcode == opcode) {
			op = &OPCODE_TABLE[x];
			break;
		}
	}

	if (op == NULL) {
		printf("Unknown opcode: 0x%02X\n", opcode);
		return(1);
	}

	for (x = 0; x < op->len; x++)
		printf("%02X ", ram[pc + x]);

	// Space padding
	x = 5 - op->len;
	while (x-- > 0)
		printf("   ");

	char str[128];

	switch(op->len) {
		case 1:
		{
			snprintf(str, sizeof(str), "%s", op->name);
			break;
		}

		case 2:
		{
			snprintf(str, sizeof(str), op->name, ram[pc + 1]);
			break;
		}

		case 3:
		{
			snprintf(str, sizeof(str), op->name, ram[pc + 2], ram[pc + 1]);
			break;
		}

		default:
		{
			break;
		}
		
	}

	printf("%s", str);

	switch(opcode) {
		// These are inversed compared to other branch opcodes, and
		// they need to be incremented by 3 rather than 2.
		case 0x13: // BBC0
		case 0x33: // BBC1
		case 0x53: // BBC2
		case 0x73: // BBC3
		case 0x93: // BBC4
		case 0xB3: // BBC5
		case 0xD3: // BBC6
		case 0xF3: // BBC7
		case 0xDE: // CBNE
		{
			printf(" ($%04X)", pc + 3 + (Sint8) ram[pc + 2]);
		}
		break;

		case 0x10: // BPL
		case 0x2F: // BRA
		case 0x30: // BMI
		case 0x50: // BVC
		case 0x70: // BVS
		case 0x90: // BCC
		case 0xB0: // BCS
		case 0xD0: // BNE
		case 0xF0: // BEQ
		case 0xFE: // DBNZ
		{
			// +2 because the operands have been read when the CPU gets ready to jump
			printf(" ($%04X)", pc + (Sint8) ram[pc + 1] + 2);
		}
		break;

		default:
			break;
	}

	printf("\n");

	return(op->len);
}

spc_file_t *read_spc_file(char *filename)
{
	FILE *f;
	Uint8 buf[1024];
	int err;
	int x;
	Uint8 *ptr;
	int has_id_tag = 0;

	spc_file_t *spc = malloc(sizeof(spc_file_t));
	if (spc == NULL) {
		perror("read_spc_file: malloc()");
		exit(1);
	}

	f = fopen(filename, "r");
	if (f == NULL) {
		perror("fopen()");
		free(spc);
		return(NULL);
	}

	// Read the whole header into a buffer first, then assign individual values.
	x = fread(buf, 1, 0x2D, f);
	if (x != 0x2D) {
		perror("spc_read_file::fread(header)");
		free(spc);
		return(NULL);
	}

	memcpy(spc->header, buf, SPC_HEADER_LEN);
	spc->header[SPC_HEADER_LEN] = '\0';

	printf("Header: [%s]\n", spc->header);

	if (memcmp(spc->header, SPC_HEADER_MAGIC, sizeof(SPC_HEADER_MAGIC)) != 0) {
		fprintf(stderr, "%s: Invalid header or version.\n", filename);
		exit(1);
	}

	if (buf[SPC_TAG_TYPE_OFFSET] == SPC_HAS_ID_TAG)
		has_id_tag = 1;

	ptr = &buf[SPC_VERSION_OFFSET];
	spc->version_minor = *ptr;

	printf("Version minor: [%d]\n", spc->version_minor);

	ptr++;

	memcpy(&spc->registers.pc, ptr, 2);
	// spc->registers.pc = ntohs(spc->registers.pc);
	spc->registers.pc = le16toh(spc->registers.pc);

	ptr += 2;

	spc->registers.a = *ptr++;
	spc->registers.x = *ptr++;
	spc->registers.y = *ptr++;

	// XXX: Make sure flags are defined in the proper order
	spc->registers.psw.val = *ptr++;

	spc->registers.sp = *ptr++;
	spc->registers.reserved[0] = *ptr++;
	spc->registers.reserved[1] = *ptr++;

	err = fseek(f, SPC_RAM_OFFSET, SEEK_SET);
	if (err != 0) {
		perror("spc_read_file: fseek(SPC_RAM_OFFSET)");
		free(spc);
		return(NULL);
	}

	x = fread(spc->ram, 1, SPC_RAM_SIZE, f);
	if (x != SPC_RAM_SIZE) {
		perror("spc_read_file: fread(SPC_RAM_SIZE)");
		free(spc);
		return(NULL);
	}

	x = fread(spc->dsp_registers, 1, SPC_DSP_REGISTERS, f);
	if (x != SPC_DSP_REGISTERS) {
		perror("spc_read_file: fread(SPC_DSP_REGISTERS)");
		free(spc);
		return(NULL);
	}

	if (has_id_tag) {
		err = fseek(f, SPC_ID_TAG_OFFSET, SEEK_SET);

		if (err != 0) {
			perror("spc_read_file::fseek(SPC_ID_TAG_OFFSET)");
			free(spc);
			return(NULL);
		}

		x = fread(spc->id_tag.song_title, 1, SPC_TAG_SONG_TITLE_LEN, f);
		if (x != SPC_TAG_SONG_TITLE_LEN) {
			perror("spc_read_file::fread(id_tag.song_title)");
			free(spc);
			return(NULL);
		}

		spc->id_tag.song_title[SPC_TAG_SONG_TITLE_LEN] = '\0';

		x = fread(spc->id_tag.game_title, 1, SPC_TAG_GAME_TITLE_LEN, f);
		if (x != SPC_TAG_GAME_TITLE_LEN) {
			perror("spc_read_file::fread(id_tag.game_title)");
			free(spc);
			return(NULL);
		}

		spc->id_tag.song_title[SPC_TAG_GAME_TITLE_LEN] = '\0';

		printf("Song title: %s\n", spc->id_tag.song_title);
		printf("Game title: %s\n", spc->id_tag.game_title);

		spc->id_tag.dumper[0] = '\0';
		spc->id_tag.comments[0] = '\0';
	} else {
		spc->id_tag.song_title[0] = '\0';
		spc->id_tag.game_title[0] = '\0';
		spc->id_tag.dumper[0] = '\0';
		spc->id_tag.comments[0] = '\0';
	}

	return(spc);
}

/* Returns the addr of the instrument for voice X. If loop is non-zero, return
 * the address of the loop instead. */
Uint16 get_sample_addr(spc_state_t *state, int voice_nr, int loop) {
	Uint16 addr;
	Uint16 addr_ptr;
	Uint16 dir;
	Uint16 voice_srcn_addr;
	Uint8 voice_srcn;

	dir = state->dsp_registers[SPC_DSP_DIR];
	voice_srcn_addr = (voice_nr << 4) | SPC_DSP_VxSCRN;
	voice_srcn = state->dsp_registers[voice_srcn_addr];

	// printf("Instrument for voice %d is %d\n", voice_nr, voice_srcn);

	/*
	 * Each entry in the 'instrument table' is 4 bytes: one word for the
	 * addr of the sample itself and another for the loop addr.
	 */
	addr_ptr = (dir << 8) | (voice_srcn * 4);

	if (loop)
		addr = read_word(state, addr_ptr + 2);
	else
		addr = read_word(state, addr_ptr);

	// printf("ptr: %04X   addr: %04X\n", addr_ptr, addr);
	// printf("loop ptr: %04X   addr: %04X\n", addr_ptr + 2, read_word(state, addr_ptr + 2));

	// printf("Sample addr is %04X\n", addr);

	return(addr);
}

typedef struct nibble_s {
	int i : 4;
} nibble_t;

brr_block_t *decode_brr_block(Uint8 *ptr) {
	Uint8 range;
	Uint8 filter;
	Uint8 loop_flag;
	Uint8 last_chunk;
	Uint8 loop_code;
	Uint8 b;
	brr_block_t *block;

	/*
	Uint8 bytes[9] = { 0xA0, 0xF0, 0x8F, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };

	ptr = bytes;
	*/

	block = malloc(sizeof(brr_block_t));
	if (NULL == block) {
		perror("decode_brr_block: malloc()");
		exit(1);
	}

	// printf("Byte 1: %02X\n", ptr[0]);

	b = ptr[0];
	range = (b & 0xF0) >> 4;
	filter = (b & 0x0C) >> 2;
	loop_flag = (b & 0x02) >> 1;
	last_chunk = (b & 0x01);
	loop_code = (b & 0x03);

	block->filter = filter;
	block->loop_flag = loop_flag;
	block->last_chunk = last_chunk;
	block->loop_code = loop_code;

	/* 
	 * Go through a constant-width struct in order to sign-extend before *
	 * scaling
	 */
	for (int x = 0; x < 8; x++) {
		Sint16 dst;
		nibble_t tmp;

		// printf("Byte %d: %02X\n", x, ptr[x + 1]);

		tmp.i = (ptr[x + 1] & 0xF0) >> 4;
		dst = tmp.i;
		dst = (dst << range) >> 1;

		// XXX: According to apudsp.txt, there should be a final shift
		// to the right here...

		block->samples[2 * x] = dst;

		tmp.i = (ptr[x + 1] & 0x0F);
		dst = tmp.i;
		dst = (dst << range) >> 1;

		block->samples[2 * x + 1] = dst;
	}

	/*
	for (int x = 0; x < 16; x++)
		printf("Sample[%d]: %d\n", x, block->samples[x]);
	*/

	return(block);
}

void dump_mem_line(spc_state_t *state, Uint16 addr) {
	int x;

	printf("$%04X", addr);

	for (x = 0; x < 16; x++) {
		printf(" %02X ", read_byte(state, addr + x));
	}

	printf("\n");
}

void dump_mem(spc_state_t *state, Uint16 addr) {
	int i;

	for (i = 0; i < 4; i++)
		dump_mem_line(state, addr + i * 16);
}

void dump_dsp(spc_state_t *state) {
	int i;
	int voice;
	Uint8 *dsp = state->dsp_registers;

	printf("== DSP Registers ==\n");

	for (i = 0; i < 127; i++) {
		voice = (i & 0xF0) >> 4;

		printf("DSP[$%02X] ", i);

		switch(i) {
			case 0x00:
			case 0x10:
			case 0x20:
			case 0x30:
			case 0x40:
			case 0x50:
			case 0x60:
			case 0x70:
				printf("Voice %d ($%02X): Vol (L): %u\n", voice, i, dsp[i]);
				break;

			case 0x01:
			case 0x11:
			case 0x21:
			case 0x31:
			case 0x41:
			case 0x51:
			case 0x61:
			case 0x71:
				printf("Voice %d ($%02X): Vol (R): %u\n", voice, i, dsp[i]);
				break;

			case 0x02:
			case 0x12:
			case 0x22:
			case 0x32:
			case 0x42:
			case 0x52:
			case 0x62:
			case 0x72:
				printf("Voice %d ($%02X): Pitch (L): %u (%02X)\n", voice, i, dsp[i], dsp[i]);
				break;

			case 0x03:
			case 0x13:
			case 0x23:
			case 0x33:
			case 0x43:
			case 0x53:
			case 0x63:
			case 0x73:
				printf("Voice %d ($%02X): Pitch (H): %u (%02X)\n", voice, i, dsp[i], dsp[i]);
				break;

			case 0x04:
			case 0x14:
			case 0x24:
			case 0x34:
			case 0x44:
			case 0x54:
			case 0x64:
			case 0x74:
				printf("Voice %d ($%02X): SRCN: %u\n", voice, i, dsp[i]);
				break;

			case 0x05:
			case 0x15:
			case 0x25:
			case 0x35:
			case 0x45:
			case 0x55:
			case 0x65:
			case 0x75:
				printf("Voice %d ($%02X): ADSR(1): %u\n", voice, i, dsp[i]);
				break;

			case 0x06:
			case 0x16:
			case 0x26:
			case 0x36:
			case 0x46:
			case 0x56:
			case 0x66:
			case 0x76:
				printf("Voice %d ($%02X): ADSR(2): %u\n", voice, i, dsp[i]);
				break;

			case 0x07:
			case 0x17:
			case 0x27:
			case 0x37:
			case 0x47:
			case 0x57:
			case 0x67:
			case 0x77:
				printf("Voice %d ($%02X): GAIN: %u\n", voice, i, dsp[i]);
				break;

			case 0x0F:
			case 0x1F:
			case 0x2F:
			case 0x3F:
			case 0x4F:
			case 0x5F:
			case 0x6F:
			case 0x7F:
				// XXX: Whether or not it's a voice depends on the source. May or not be per-voice?
				printf("Voice %d ($%02X): FILTER: %u\n", voice, i, dsp[i]);
				break;

			case 0x08:
				printf("*ENVX: %u\n", dsp[i]);
				break;

			case 0x09:
				printf("*VALX: %u\n", dsp[i]);
				break;

			case 0x0C:
				printf("MASTVOLL: %u\n", dsp[i]);
				break;

			case 0x0D:
				printf("ECHO: %u\n", dsp[i]);
				break;

			case 0x1C:
				printf("MASTVOLR: %u\n", dsp[i]);
				break;

			case 0x2C:
				printf("ECHOVOL (L): %u\n", dsp[i]);
				break;

			case 0x2D:
				printf("PMON: %u\n", dsp[i]);
				break;

			case 0x3C:
				printf("ECHOVOL (R): %u\n", dsp[i]);
				break;

			case 0x3D:
				printf("NOV: $#%02X\n", dsp[i]);
				break;

			case 0x4C:
				printf("KON: $#%02X\n", dsp[i]);
				break;

			case 0x4D:
				printf("EOV: $#%02X\n", dsp[i]);
				break;

			case 0x5C:
				printf("KOFF: $#%02X\n", dsp[i]);
				break;

			case 0x5D:
				printf("SAMLOC: $#%02X\n", dsp[i]);
				break;

			case 0x6C:
				printf("FLG: $#%02X\n", dsp[i]);
				break;

			case 0x7C:
				printf("*ENDX: $#%02X\n", dsp[i]);
				break;

			default:
				printf("\n");
				break;
		}
	}
}

void usage(char *argv0)
{
	printf("Usage: %s <filename.spc>\n", argv0);
}

void show_menu(void) {
	printf("b <addr>   Set breakpoint on <addr> (ie, \"b abcd\")\n");
	printf("c          Continue execution\n");
	printf("d [<addr>] Disassemble at $<addr>, or $pc if addr is not supplied (ie, \"d abcd\")\n");
	printf("h          Shows this help\n");
	printf("n          Execute next instruction\n");
	printf("p          Enable/disable profiling\n");
	printf("sd         Show DSP Registers\n");
	printf("sr         Show CPU Registers\n");
	printf("sr         Show profiling counters\n");
	printf("ta         Enable/disable ALL tracing \n");
	printf("td         Enable/disable DSP Operations tracing \n");
	printf("ti         Enable/disable instruction tracing \n");
	printf("tj         Enable/disable jump/call tracing \n");
	printf("tt         Enable/disable timer/counters tracing\n");
	printf("tr         Enable/disable register read/write tracing \n");
	printf("tv         Enable/disable voice-register tracing\n");
	printf("w <nr>     Write sample <nr> to disk\n");
	printf("x <mem>    Examine memory at $<mem> (ie, \"x abcd\")\n");
	printf("<Enter>    Execute next instruction\n");
}

int get_voice_pitch(spc_state_t *state, int voice_nr) {
	Uint8 pitch_low;
	Uint8 pitch_high;
	int pitch;

	pitch_low = get_dsp_voice(state, voice_nr, SPC_DSP_VxPITCHL);
	pitch_high = get_dsp_voice(state, voice_nr, SPC_DSP_VxPITCHH);

	pitch = make16(pitch_high, pitch_low);

	return(pitch);
}

void audio_callback(void *userdata, Uint8 * stream, int len) {
	int voice_nr;
	spc_state_t *state = (spc_state_t *) userdata;
	Sint16 sample;

	printf("audio_callback(len=%d)\n", len);

	for (voice_nr = 0; voice_nr < 8; voice_nr++) {
		if (state->voices[voice_nr].enabled) {
			sample = get_next_sample(state, voice_nr);
		}
	}
}

/* 
 * Decode the next BRR block for this voice.
 *
 * Returns 1 if another block could be decoded, 0 if this is it
 */
int decode_next_brr_block(spc_state_t *state, int voice_nr) {
		int ret = 1;
		spc_voice_t *v = &state->voices[voice_nr];
		brr_block_t *block = v->block;

		/* kon() initializes v->block, so block should never be NULL
		 * when we get here.
		 */
		assert(NULL != v->block);

		printf("last_chunk? %d\n", block->last_chunk);
		printf("loop? %d\n", block->loop_flag);

		v->cur_addr += 9;

		if (v->block->last_chunk) {
			if (v->block->loop_flag) {
				v->cur_addr = get_sample_addr(state, voice_nr, 1);
			} else {
				// XXX: Should the koff() be done here or the caller?
				ret = 0;
			}

			free(v->block);
		}

		if (ret) {
			v->block = decode_brr_block(&state->ram[v->cur_addr]);
			v->next_sample = 0;
		}

		return(ret);
}

Sint16 get_next_sample(spc_state_t *state, int voice_nr) {
	int done = 0;
	spc_voice_t *v = &state->voices[voice_nr];
	Sint16 sample;

	if (v->next_sample > 15) {
		done = decode_next_brr_block(state, voice_nr);
		v->next_sample = 0;
	}

	// XXX: Pitch can probably change while voice is playing.

	if (done) {
		v->enabled = 0;

		// Silence
		sample = 0;
	} else {
		int brr_nr = ((v->counter & 0xF000) >> 12) & 0xF;
		int index = ((v->counter & 0x0FF0) >> 4);

		assert(brr_nr <= 15);
		assert(index < sizeof(INTERP_TABLE) / sizeof(INTERP_TABLE[0]));

		sample = v->block->samples[brr_nr];

		// Uint16 out = (INTERP_TABLE[0x00 + index] * sample) >> 10;
		// Sint16 sample = (INTERP_TABLE[0x00 + index] * sample);

		v->counter += v->step;
	}

	return(sample);
}

/* Called when a voice is Keyed-ON ("KON") */
void kon_voice(spc_state_t *state, int voice_nr) {
	int pitch;

	pitch = get_voice_pitch(state, voice_nr);

	state->voices[voice_nr].enabled = 1;
	state->voices[voice_nr].cur_addr = get_sample_addr(state, voice_nr, 0);
	state->voices[voice_nr].next_sample = 0;
	state->voices[voice_nr].looping = 0;
	
	// XXX: Include PMON
	state->voices[voice_nr].step = pitch;
	state->voices[voice_nr].counter = 0;

	/* KON can be called while the voice is already enabled */
	if (NULL != state->voices[voice_nr].block)
		free(state->voices[voice_nr].block);

	Uint8 *block_ptr = &state->ram[state->voices[voice_nr].cur_addr];

	state->voices[voice_nr].block = decode_brr_block(block_ptr);
}

/* Called when a voice is Keyed-OFF ("KOFF") */
void koff_voice(spc_state_t *state, int voice_nr) {
	state->voices[voice_nr].enabled = 0;

	if (NULL != state->voices[voice_nr].block)
		free(state->voices[voice_nr].block);
}

/* Initialize a voice to a default state at power-up */
void init_voice(spc_state_t *state, int voice_nr) {
	state->voices[voice_nr].enabled = 0;
	state->voices[voice_nr].cur_addr = 0;
	state->voices[voice_nr].next_sample = 0;
	state->voices[voice_nr].looping = 0;
	state->voices[voice_nr].block = NULL;
	state->voices[voice_nr].step = 0;
	state->voices[voice_nr].prev_brr_samples[0] = 0;
	state->voices[voice_nr].prev_brr_samples[1] = 0;
	state->voices[voice_nr].prev_brr_samples[2] = 0;
	state->voices[voice_nr].prev_brr_samples[3] = 0;
	state->voices[voice_nr].counter = 0;
}

int init_audio(char *wanted_device, spc_state_t *state) {
	int ret = SUCCESS;
	int err;
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;

	err = SDL_Init(SDL_INIT_AUDIO);

	if (0 != err) {
		fprintf(stderr, "ERROR: SDL_Init(): %s\n", SDL_GetError());
		exit(1);
	}

	SDL_zero(desired);

	desired.freq = 32000;		// SPC Samples are played at 32kHz, I believe.
	desired.format = AUDIO_S16;	// SPC samples are signed 16-bit samples
	desired.samples = 512;		// Don't attempt to queue too many samples since we have to interpret in real-time
	desired.size = 0;		// Will be initialized by SDL_OpenAudio()
	desired.silence = 0;		// Will be initialized by SDL_OpenAudio()
	desired.callback = audio_callback;
	desired.userdata = state;

	err = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);

	if (err < 0) {
		fprintf(stderr, "ERROR: SDL_OpenAudioDevice(): %s\n", SDL_GetError());
		exit(1);
	}

	printf("SDL_OpenAudioDevice(): Obtained freq: %d\n", obtained.freq);
	printf("SDL_OpenAudioDevice(): Obtained format: %d (AUDIO_S16 == %d)\n", obtained.format, AUDIO_S16);
	printf("SDL_OpenAudioDevice(): Obtained samples: %d\n", obtained.samples);

#if 0
	int num_drivers;
	int idx = -1;
	char *dev_name;

	num_drivers = SDL_GetNumAudioDrivers();

	for (int x = 0; x < num_drivers; x++) {
		dev_name = SDL_GetAudioDriver(x);
		printf("Device %d: %s\n", x, dev_name);

		if (wanted_device != NULL && strcmp(wanted_device, dev_name) == 0) {
			idx = x;
		}
	}

	/* User has not specified a device, or it was not found */
	if (idx == -1) {
		if (wanted_device != NULL) {
			fprintf(Stderr, "ERROR: Could not find device %s\n", wanted_device);
			err = FATAL_ERROR;
		} else {
			idx = 0;
		}
	}

	if (idx >= 0) {
		dev_name = SDL_GetAudioDriver(idx);
		if (SDL_AudioInit(dev_name))
	}
#endif

	printf("%s\n", SDL_GetError());

	printf("Current audio driver: %s\n", SDL_GetCurrentAudioDriver());

	return(ret);
}

typedef struct prof_struct {
	Uint16 addr;
	int hits;
} prof_t;

int compare_profs(const void *a, const void *b) {
	const prof_t *pa, *pb;
	int ret;

	pa = a;
	pb = b;

	if (pa->hits < pb->hits)
		ret = -1;
	else if (pa->hits == pb->hits) {
		/* Identical counts are ordered by their address, which is unique */
		if (pa->addr < pb->addr)
			ret = -1;
		else
			ret = 1;
	} else
		ret = 1;

	return(ret);
}

void dump_profiling(spc_state_t *state) {
	int x;
	prof_t *tmp;

	if (state->profile_info) {
		tmp = malloc(sizeof(prof_t) * 65536);

		for (x = 0; x < 65536; x++) {
			tmp[x].addr = x;
			tmp[x].hits = state->profile_info[x];
		}

		qsort(tmp, 65536, sizeof(prof_t), compare_profs);
		for (x = 0; x < 65536; x++) {
			if (tmp[x].hits > 0) {
				printf("%-10d ", tmp[x].hits);
				dump_instruction(tmp[x].addr, state->ram);
			}
		}

		free(tmp);
	} else {
		printf("Profiling not enabled.\n");
	}

}

void enable_profiling(spc_state_t *state) {
	int x;

	if (! state->profile_info) {
		state->profile_info = malloc (sizeof(int) * 65536);

		for (x = 0; x < 65536; x++) {
			state->profile_info[x] = 0;
		}
	}
}

void disable_profiling(spc_state_t *state) {
	if (state->profile_info) {
		free(state->profile_info);
		state->profile_info = NULL;
	}
}

void handle_sigint(int sig) {
	g_do_break = 1;
}

/* Returns true if the code is looping on a timer status */
int is_waiting_on_timer(Uint8 *mem) {
	int ret = 0;

	unsigned char pattern1[5] = { 0xEC, 0xFD, 0x00, 0xF0, 0xFB }; // MOV Y,$00FD; BEQ $00FB

	if (memcmp(mem, pattern1, 5) == 0) {
		ret = SPC_REG_TIMER0;
	}

	return(ret);
}

int main (int argc, char *argv[])
{
	spc_file_t *spc_file;
	spc_state_t state;
	char input[200];
	int quit = 0;
	int break_addr = -1;
	char *device = NULL;
	sig_t err;

	if (argc != 2) {
		usage(argv[0]);
		exit(1);
	}

	/* XXX: Allow audio-less mode. For example, when converting to a wav */
	if (init_audio(device, &state) != SUCCESS) {
		fprintf(stderr, "Could not initialize audio\n");
		exit(1);
	}

	spc_file = read_spc_file(argv[1]);
	if (spc_file == NULL) {
		fprintf(stderr, "Error loading file %s\n", argv[1]);
		exit(1);
	}

	state.regs = &spc_file->registers;
	state.ram = spc_file->ram;
	state.cycle = 0;
	state.dsp_registers = spc_file->dsp_registers;
	state.trace = 0;
	state.profiling = 0;
	state.profile_info = NULL;

	// Assume that whatever was in DSP_ADDR is the current register.
	state.current_dsp_register = state.ram[0xF2];

	/* Initialize timers. Assume they are enabled */
	// XXX: They might be disabled. Just initialize based on the state in CONTROL..
	state.timers.counter[0] = state.ram[SPC_REG_COUNTER0];
	state.timers.counter[1] = state.ram[SPC_REG_COUNTER1];
	state.timers.counter[2] = state.ram[SPC_REG_COUNTER2];

	enable_timer(&state, 0);
	enable_timer(&state, 1);
	enable_timer(&state, 2);

	// XXX: Voices enable should come from KON on startup?
	for (int x = 0; x < 8; x++)
		init_voice(&state, x);

	printf("PC: $%04X\n", state.regs->pc);

	// decode_brr_block(&state.ram[0x1000]);

	err = signal(SIGINT, handle_sigint);
	if (SIG_ERR == err) {
		perror("signal(SIGINT)");
		exit(1);
	}

	while (! quit) {
		if (state.regs->pc == break_addr) {
			printf("Reached breakpoint %04X\n", break_addr);
			g_do_break = 1;
		}

		// Should we break after every instruction?
		if (g_do_break) {
			// dump_registers(state.regs);
			dump_instruction(state.regs->pc, state.ram);

			printf("> ");
			fflush(stdout);
			char *err = fgets(input, sizeof(input) - 1, stdin);

			if (err == NULL) {
				quit = 1;
				break;
			}

			switch(input[0]) {
				case '?':
					show_menu();
					break;

				case 'b': // break
				{
					char *ptr = strchr(input, ' ');

					if (ptr) {
						break_addr = (Uint16) strtol(ptr, NULL, 16);
						printf("Breakpoint enabled at %04X\n", break_addr);
					} else {
						fprintf(stderr, "Missing argument\n");
					}
				}
				break;

				case 'c': // continue
				{
					printf("Continue.\n");
					g_do_break = 0;
				}
				break;

				case 'd': // disasm
				{
					Uint16 addr;
					int len;
					int x;
					char *ptr = strchr(input, ' ');

					if (ptr) {
						addr = (Uint16) strtol(ptr, NULL, 16);
					} else {
						addr = state.regs->pc;
					}

					for (x = 0; x < 15; x++) {
						len = dump_instruction(addr, state.ram);
						addr = addr + len;
					}
				}
				break;

				case 'h':
					show_menu();
					break;

				case '\n':
				case 'n':
					execute_next(&state);
					break;

				case 'p':
					state.profiling = !state.profiling;
					printf("Profiling is now %s.\n", state.profiling ? "enabled" : "disabled");

					if (state.profiling)
						enable_profiling(&state);
					else
						disable_profiling(&state);

					break;

				case 'q':
					quit = 1;
					break;

				case 's':
				{
					if (strlen(input) < 3) {
						fprintf(stderr, "Unknown command\n");
						show_menu();
					} else {
						switch (input[1]) {
							case 'd':
								dump_dsp(&state);
								break;

							case 'p':
								dump_profiling(&state);
								break;

							case 'r':
								dump_registers(state.regs);
								break;

							default:
								fprintf(stderr, "Unknown command\n");
								show_menu();
								break;
						}
					}
				}
				break;

				case 't':
				{
					if (strlen(input) == 3) {
						switch(input[1]) {
							case 'a' :
							{
								if ((state.trace & TRACE_ALL) != TRACE_ALL)
									state.trace = TRACE_ALL;
								else
									state.trace = 0;

								printf("Instruction tracing is now %s.\n", state.trace & TRACE_CPU_INSTRUCTIONS ? "enabled" : "disabled");
								printf("Jump/Call tracing is now %s.\n", state.trace & TRACE_CPU_JUMPS ? "enabled" : "disabled");
								printf("Register read/write tracing is now %s.\n", state.trace & TRACE_REGISTER_WRITES ? "enabled" : "disabled");
								printf("Timers tracing is now %s.\n", state.trace & TRACE_APU_VOICES ? "enabled" : "disabled");
								printf("Voices tracing is now %s.\n", state.trace & TRACE_APU_VOICES ? "enabled" : "disabled");
							}
							break;

							case 'd' :
							{
								state.trace ^= TRACE_DSP_OPS;
								printf("DSP Operations tracing is now %s.\n", state.trace & TRACE_DSP_OPS ? "enabled" : "disabled");
							}
							break;

							case 'i' :
							{
								state.trace ^= TRACE_CPU_INSTRUCTIONS;;
								printf("Instruction tracing is now %s.\n", state.trace & TRACE_CPU_INSTRUCTIONS ? "enabled" : "disabled");
							}
							break;

							case 'j' :
							{
								state.trace ^= TRACE_CPU_JUMPS;;
								printf("Jump/Call tracing is now %s.\n", state.trace & TRACE_CPU_JUMPS ? "enabled" : "disabled");
							}
							break;

							case 'r' :
							{
								state.trace ^= TRACE_REGISTER_WRITES;;
								state.trace ^= TRACE_REGISTER_READS;;
								printf("Register read/write tracing is now %s.\n", state.trace & TRACE_REGISTER_WRITES ? "enabled" : "disabled");
							}
							break;

							case 't' :
							{
								state.trace ^= TRACE_COUNTERS;;
								printf("Timers tracing is now %s.\n", state.trace & TRACE_COUNTERS ? "enabled" : "disabled");
							}
							break;

							case 'v' :
							{
								state.trace ^= TRACE_APU_VOICES;
								printf("Voices tracing is now %s.\n", state.trace & TRACE_APU_VOICES ? "enabled" : "disabled");
							}
							break;

							default:
							{
								fprintf(stderr, "Unknown trace, '%c'\n", input[1]);
							}
							break;
						}
					} else {
						fprintf(stderr, "Missing argument to trace\n");
					}
				}
				break;

				case 'w':
				{
					if (strlen(input) < 4) {
						fprintf(stderr, "Missing argument\n");
					} else {
						int voice_nr = atoi(&input[2]);

						if (voice_nr >= 0 && voice_nr <= 7) {
							dump_voice(&state, voice_nr, NULL);
						} else {
							fprintf(stderr, "Error: voice must be between 0 and 7\n");
						}
					}
				}
				break;

				case 'x':
				{
					char *ptr = strchr(input, ' ');

					if (ptr) {
						Uint16 addr = (Uint16) strtol(ptr, NULL, 16);
						dump_mem(&state, addr);
					} else {
						fprintf(stderr, "Missing argument\n");
					}
				}
				break;

				default:
					fprintf(stderr, "Unknown command, %c\n", input[0]);
					break;
			}
		} else {
			// dump_registers(state.regs);
			if (state.trace & TRACE_CPU_INSTRUCTIONS)
				dump_instruction(state.regs->pc, state.ram);

			/*
			if (is_waiting_on_timer(&state.ram[state.regs->pc])) {
				printf("$%04X Waiting on timer to expire\n", state.regs->pc);
			}
			*/

			execute_next(&state);
			update_counters(&state);

			if ((state.cycle % (2048 * 1000)) <= 6) {
				printf("Seconds elapsed: %0.1f\n", (float) state.cycle / (2048 * 1000));
			}
		}
	}

	return (0);
}
