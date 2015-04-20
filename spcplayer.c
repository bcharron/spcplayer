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

#include "opcodes.h"

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


/* Passed to functions that may or not update flags */
#define DONT_ADJUST_FLAGS 0
#define ADJUST_FLAGS 1

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
	Uint8 counter[3];		// Increments by one every time next_timer == cycle
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

typedef struct spc_state_s {
	spc_registers_t *regs;
	spc_timers_t timers;
	Uint8 *ram;
	Uint8 *dsp_registers;
	unsigned long cycle;
} spc_state_t;

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
void set_timer(spc_state_t *state, int timer, Uint8 value);

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

void register_write(spc_state_t *state, Uint16 addr, Uint8 val) {
	// printf("Register write $#%02X into $%04X\n", val, addr);

	switch(addr) {
		case 0xF0:	// Test?
			state->ram[addr] = val;
			break;

		case 0xF1:	// Control
			state->ram[addr] = val;

			// Start or stop a timer
			for (int timer = 0; timer < 3; timer++) {
				int bit = 0x01 << timer;

				// XXX: Handle the case where timer == 0x00, which is in fact 256.
				if (val & bit)
					set_timer(state, timer, val);
			}

			// XXX: Handles bits 4-5 (PORT0-3)
			// XXX: Bit 7 appears to be related to the IPL ROM being ROM or RAM.
			break;

		case 0xF2:	// Register stuff: Add
			state->ram[addr] = val;
			break;

		case 0xF3:	// Register stuff: Data
			state->ram[addr] = val;
			break;

		case 0xF4:	// I/O Ports
		case 0xF5:
		case 0xF6:
		case 0xF7:
			state->ram[addr] = val;
			break;

		case 0xF8:	// Unknown
		case 0xF9:
			state->ram[addr] = val;
			break;

		case 0xFA:	// Timers
		case 0xFB:
		case 0xFC:
			state->ram[addr] = val;
			break;

		case 0xFD:	// Counters
		case 0xFE:
		case 0xFF:
			state->ram[addr] = val;
			break;

		default:
			fprintf(stderr, "register_write(%04X): HOW THE FUCK DID THIS HAPPEN??\n", addr);
			exit(1);
			break;
	}
}

Uint8 register_read(spc_state_t *state, Uint16 addr) {
	Uint8 val;

	// printf("Register read $%04X\n", addr);

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
			val = state->ram[addr];
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

		case 0xFA:	// Timers
		case 0xFB:
		case 0xFC:
			val = state->ram[addr];
			break;

		case 0xFD:	// Counters
		case 0xFE:
		case 0xFF:
			val = state->ram[addr];
			state->ram[addr] = 0;
			break;

		default:
			val = state->ram[addr];
			break;
	}

	return(val);
}

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

/* Perform the branch if flag 'flag' is set */
int branch_if_flag(spc_state_t *state, int flag, Uint8 operand1) {
	int cycles;

	if (flag) {
		state->regs->pc += (Sint8) operand1 + 2;
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
	Uint8 test;
	
	test = 1 << bit;

	if (state->ram[src_addr] & test) {
		cycles = 5;
		state->regs->pc += 3;
	} else {
		state->regs->pc += (Sint8) rel + 3;
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

	printf("Popped address %04X\n", ret_addr);

	state->regs->pc = ret_addr;
	printf("Jumping to $%04X\n", state->regs->pc);
}

void do_call(spc_state_t *state, Uint8 operand1, Uint8 operand2) {
	Uint16 ret_addr;
	Uint16 dest_addr;

	ret_addr = state->regs->pc + 3;

	printf("Pushing return address $%04X on the stack\n", ret_addr);

	do_push(state, get_high(ret_addr));
	do_push(state, get_low(ret_addr));

	dest_addr = make16(operand2, operand1);
	state->regs->pc = dest_addr;

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
	printf("Result: %08x\n", result);
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
	printf("Result: %08x\n", result);
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

			state->timers.counter[timer]++;

			// XXX: I *think* this should correctly handle the case where the desired timer = 0x00
			if (state->timers.counter[timer] == state->ram[SPC_REG_TIMER0 + timer]) {
				// Only the bottom 4 bits of the counter are available.
				state->ram[SPC_REG_COUNTER0 + timer] = (state->ram[SPC_REG_COUNTER0 + timer] + 1) % 16;

				// Internal counter resets when the timer hits.
				state->timers.counter[timer] = 0;

				printf("TIMER %d HIT: %d\n", timer, state->ram[SPC_REG_COUNTER0 + timer]);
			}
		}
	}
}

void set_timer(spc_state_t *state, int timer, Uint8 value) {
	state->timers.next_timer[timer] = state->cycle + TIMER_CYCLES[timer];
	state->timers.counter[timer] = 0;	// Internal counter, increased every clock
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

		case 0x04: // ORZ A, $E8
			val = get_direct_page_byte(state, operand1);
			state->regs->a |= val;
			adjust_flags(state, state->regs->a);
			cycles = 3;
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

		case 0x13: // BBC0 $00xx, $yy
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

		case 0x24: // ANDZ A, $xx
			val = get_direct_page_byte(state, operand1);
			state->regs->a &= val;
			cycles = 2;
			break;

		case 0x2D: // PUSH A
			do_push(state, state->regs->a);
			cycles = 4;
			break;

		case 0x2F: // BRA xx
			branch_if_flag(state, 1, operand1);
			cycles = 4;
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

		case 0x60: // CLRC
			state->regs->psw.f.c = 0;
			cycles = 2;
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

		case 0x7D: // MOV A, X
			state->regs->a = state->regs->x;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x80: // SETC
			state->regs->psw.f.c = 1;
			cycles = 2;
			break;

		case 0x8D: // MOV Y,#$xx
			state->regs->y = operand1;
			adjust_flags(state, state->regs->y);
			cycles = 2;
			break;

		case 0x8F: // MOV $dp, #$xx
			dp_addr = get_direct_page_addr(state, operand2);
			write_byte(state, dp_addr, operand1);
			cycles = 5;
			break;

		case 0x90: // BCC
			// cycles = do_bcc(state, state->ram[addr + 1]);
			cycles = branch_if_flag_clear(state, state->regs->psw.f.c, operand1);
			pc_adjusted = 1;
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

		case 0x9F: // XCN A
			state->regs->a = ((state->regs->a << 4) & 0xF0) | (state->regs->a >> 4);
			adjust_flags(state, state->regs->a);
			cycles = 5;
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
			// cycles = do_bne(state, operand1);
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
			dp_addr = get_direct_page_addr(state, operand2);
			dp_addr += state->regs->x;
			val = read_byte(state, dp_addr);
			do_cmp(state, state->regs->a, val);
			branch_if_flag_clear(state, state->regs->psw.f.z, operand1);
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

		case 0xE8: // MOV A, #$xx
			state->regs->a = operand1;
			adjust_flags(state, state->regs->a);
			cycles = 2;
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

		case 0xF0: // BEQ
			// cycles = do_beq(state, operand1);
			cycles = branch_if_flag_set(state, state->regs->psw.f.z, operand1);
			pc_adjusted = 1;
			break;

		case 0xF2: // CLR7 $11
			dp_addr = get_direct_page_addr(state, operand1);
			state->ram[dp_addr] &= (~ 0x80);
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

	execute_instruction(state, state->regs->pc);

	return(0);
}

void dump_registers(spc_registers_t *registers)
{
	printf("== Registers==\n");
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
		case 0x13: // BBC0
		case 0x33: // BBC1
		case 0x53: // BBC2
		case 0x73: // BBC3
		case 0x93: // BBC4
		case 0xB3: // BBC5
		case 0xD3: // BBC6
		case 0xF3: // BBC7
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
		case 0xDE: // CBNE
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

void usage(char *argv0)
{
	printf("Usage: %s <filename.spc>\n", argv0);
}

void show_menu(void) {
	printf("b <addr>   Set breakpoint on <addr> (ie, \"b abcd\")\n");
	printf("c          Continue execution\n");
	printf("d [<addr>] Disassemble at $<addr>, or $pc if addr is not supplied (ie, \"d abcd\")\n");
	printf("h          Shows this help\n");
	printf("i          Show registers\n");
	printf("n          Execute next instruction\n");
	printf("x <mem>    Examine memory at $<mem> (ie, \"x abcd\")\n");
}

int main (int argc, char *argv[])
{
	spc_file_t *spc_file;
	spc_state_t state;
	char input[200];
	int quit = 0;
	int do_break = 1;
	int break_addr = -1;

	if (argc != 2) {
		usage(argv[0]);
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

	/* Initialize timers. Assume they are enabled */
	set_timer(&state, 0, state.ram[SPC_REG_COUNTER0]);
	set_timer(&state, 1, state.ram[SPC_REG_COUNTER1]);
	set_timer(&state, 2, state.ram[SPC_REG_COUNTER2]);

	printf("PC: $%04X\n", state.regs->pc);

	while (! quit) {
		if (state.regs->pc == break_addr) {
			printf("Reached breakpoint %04X\n", break_addr);
			do_break = 1;
		}

		// Should we break after every instruction?
		if (do_break) {
			dump_registers(state.regs);
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
					printf("Continue.\n");
					do_break = 0;
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

				case 'i':
					dump_registers(state.regs);
					break;

				case '\n':
				case 'n':
					execute_next(&state);
					break;

				case 'q':
					quit = 1;
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

			}

		} else {
			dump_registers(state.regs);
			dump_instruction(state.regs->pc, state.ram);
			execute_next(&state);
			update_counters(&state);
		}
	}

	return (0);
}
