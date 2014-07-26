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
#include <SDL/SDL.h>

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

#define NO_OPERAND 0

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
	Uint8 *ram;
	Uint8 *dsp_registers;
	long cycle;
} spc_state_t;

int dump_instruction(Uint16 pc, Uint8 *ram);
void dump_registers(spc_registers_t *registers);
int execute_next(spc_state_t *state);
spc_file_t *read_spc_file(char *filename);
Uint16 get_direct_page_addr(spc_state_t *state, Uint16 addr);
Uint8 get_direct_page_byte(spc_state_t *state, Uint16 addr);

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

/* Make a 16-bit value out of two 8-bit ones */
uint16_t make16(uint8_t high, uint8_t low) {
	uint16_t offset = ((uint16_t) high << 8) | low;

	return(offset);
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


int do_beq(spc_state_t *state, Uint8 operand1)
{
	int cycles = 2;
	// printf("BEQ 0x%02X\n", operand1);

	if (state->regs->psw.f.z) {
		state->regs->pc += (Sint8) operand1 + 2;
		printf("Jumping to 0x%04X\n", state->regs->pc);
	} else {
		state->regs->pc += 2;
		cycles = 4;
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

Uint8 do_sbc(spc_state_t *state, Uint8 dst, Uint8 operand) {
	Uint16  result;
	Uint16 sResult;
	Uint8 ret;

	result = dst - operand - (! state->regs->psw.f.c);
	sResult = (Uint8) dst - (Uint8) operand - (! state->regs->psw.f.c);
	ret = result & 0x00FF;

	state->regs->psw.f.c = !(result > 0xFF);
	state->regs->psw.f.n = ((ret & 0x80) != 0);
	state->regs->psw.f.v = (sResult < -128 || sResult > 127);
	state->regs->psw.f.z = (ret == 0);

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
	ret = state->ram[real_addr];

	return(ret);
}

/* Adjust Zero and Negative flag based on value in 'val' */
void adjust_flags(spc_state_t *state, Uint16 val) {
	state->regs->psw.f.n = (val & 0x80) > 0;
	state->regs->psw.f.z = (val == 0);
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

	// dump_registers(state->regs);
	// dump_instruction(addr, state->ram);

	opcode = state->ram[addr];
	operand1 = state->ram[addr + 1];
	operand2 = state->ram[addr + 2];

	opcode_ptr = get_opcode_by_value(opcode);

	switch(opcode) {
		case 0x24: // ANDZ A, $xx
			val = get_direct_page_byte(state, operand1);
			state->regs->a &= val;
			cycles = 2;
			break;

		case 0x5C: // LSR A
			state->regs->psw.f.c = state->regs->a & 0x01;
			state->regs->a = state->regs->a >> 1;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;

		case 0x60: // CLRC
			state->regs->psw.f.c = 0;
			cycles = 2;
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

		case 0x9F: // XCN A
			state->regs->a = ((state->regs->a << 4) & 0xF0) | (state->regs->a >> 4);
			adjust_flags(state, state->regs->a);
			cycles = 5;
			break;

		case 0xB6: // SBC A, $xx + Y
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->y;

			val = state->ram[abs_addr];
			
			state->regs->a = do_sbc(state, state->regs->a, val);
			// state->regs->a = state->regs->a - val - state->regs->psw.f.c;
			cycles = 5;
			break;

		case 0xC4: // MOVZ $xx, A
			dp_addr = get_direct_page_addr(state, operand1);
			state->ram[dp_addr] = state->regs->a;
			cycles = 4;
			break;

		case 0xCF: // MUL YA
		{
			Uint16 result = state->regs->y * state->regs->a;

			// XXX: Is it the other way around?
			state->regs->y = (result & 0xFF00) >> 8;
			state->regs->a = (result & 0x00FF);
			cycles = 9;
		}
		break;

		case 0xDA: // MOVW $xx, YA
			dp_addr = get_direct_page_addr(state, operand1);

			state->ram[dp_addr] = state->regs->y;
			state->ram[dp_addr + 1] = state->regs->a;	// XXX: Is it the other way around?
			cycles = 4;	// XXX: One source says 4, another 5..
			break;

		case 0xDD: // MOV A, Y
			state->regs->a = state->regs->y;
			adjust_flags(state, state->regs->a);
			cycles = 2;
			break;


		case 0xE4: // MOVZ A, $xx
			val = get_direct_page_byte(state, operand1);
			state->regs->a = val;
			adjust_flags(state, state->regs->a);
			cycles = 3;
			break;

		case 0xEB: // MOV Y, $xx
			val = get_direct_page_byte(state, operand1);
			state->regs->y = val;
			adjust_flags(state, state->regs->y);
			cycles = 3;
			break;

		case 0xF0: // BEQ
			cycles = do_beq(state, state->ram[addr + 1]);
			break;

		case 0xF5: // MOV A, $xxxx + X
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->x;
			state->regs->a = state->ram[abs_addr];
			adjust_flags(state, state->regs->a);
			cycles = 5;
			break;

		case 0xF6: // MOV A, $xxxx + Y
			abs_addr = make16(operand2, operand1);
			abs_addr += state->regs->y;
			state->regs->a = state->ram[abs_addr];
			adjust_flags(state, state->regs->a);
			cycles = 5;
			break;
			
		case 0xFD: // MOV Y, A
			state->regs->y = state->regs->a;
			adjust_flags(state, state->regs->y);
			cycles = 2;
			break;

		default:
			fprintf(stderr, "Instruction #$%02X at $%04X not implemented\n", opcode, addr);
			exit(1);
			break;
	}

	/* Increment PC if not a branch */
	switch(opcode) {
		case 0xF0:
			// PC already modified accordingly.
			break;

		default:
			state->regs->pc += opcode_ptr->len;
			break;
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
		case 0xF0: // BEQ
		{
			// +2 because the operands have been read when the CPU gets ready to jump
			printf(" ($%04X)", pc + ram[pc + 1] + 2);
			break;
		}

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
	spc->header[SPC_HEADER_LEN + 1] = '\0';

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
		printf(" %02X ", state->ram[addr + x]);
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

	printf("si: %lu\n", sizeof(spc_flags_t));

	state.regs = &spc_file->registers;
	state.ram = spc_file->ram;
	state.cycle = 0;
	state.dsp_registers = spc_file->dsp_registers;

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
					char *ptr = strchr(input, ' ');

					if (ptr) {
						addr = (Uint16) strtol(ptr, NULL, 16);
						dump_instruction(addr, state.ram);
					} else {
						addr = state.regs->pc;
					}

					dump_instruction(addr, state.ram);
				}
				break;

				case 'h':
					show_menu();
					break;

				case 'i':
					dump_registers(state.regs);
					break;

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
		}
	}

	return (0);
}
