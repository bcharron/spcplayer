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

#include <stdio.h>
#include <sys/time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <SDL/SDL.h>

#include "opcode_table.h"

#define SPC_HEADER_LEN 33
#define SPC_HEADER_CONTAINS_ID_TAG
#define SPC_VERSION_OFFSET 0x24
#define SPC_RAM_OFFSET 0x0100
#define SPC_DSP_REGISTERS 128
#define SPC_RAM_SIZE 65536

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
} spc_file_t;

typedef struct spc_state_s {
	spc_registers_t *regs;
	Uint8 *ram;
	Uint8 *dsp_registers;
	long cycle;
} spc_state_t;

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

void do_beq(spc_state_t *state, Uint8 operand1)
{
	printf("BEQ 0x%02X\n", operand1);

	if (state->regs->psw.f.z) {
		state->cycle += 2;
		state->regs->pc += (Sint8) operand1;
		printf("Jumping to 0x%04X\n", state->regs->pc);
	} else {
		state->regs->pc += 2;
	}
}

void instr_or(spc_state_t *state, Uint8 *operand1, Uint8 operand2)
{
	printf("OR %02X, %02X\n", *operand1, operand2);
	*operand1 = *operand1 | operand2;

	state->regs->psw.f.n = (*operand1 & 0x80) > 0;
	state->regs->psw.f.z = (*operand1 == 0);
}

void do_or(spc_state_t *state, Uint8 opcode)
{
	Uint16 operand16;

	switch(opcode) {
		case 0x04:
			instr_or(state, &state->regs->a, state->ram[state->regs->pc + 1]);
			break;

		case 0x05:
			memcpy(&operand16, &state->ram[state->regs->pc + 1], 2);
			instr_or(state, &state->regs->a, state->ram[operand16]);
			break;

		case 0x06:
			instr_or(state, &state->regs->a, state->ram[state->regs->x]);
			break;
	}
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
		if (opcode_table[x].opcode == opcode) {
			op = &opcode_table[x];
			break;
		}
	}

	if (op == NULL) {
		printf("Unknown opcode: 0x%02X\n", opcode);
		return(1);
	}

	for (x = 0; x < op->bytes; x++)
		printf("%02X ", ram[pc + x]);

	// Space padding
	x = 5 - op->bytes;
	while (x-- > 0)
		printf("   ");

	printf(" %s\n", op->name);

/*
	switch(opcode) {
		case 0x6B: // RORZ
			bytes = 2;
			printf("RORZ 0x%02X\n", ram[pc + 1]);
			break;

		case 0x6C: // ROR
			bytes = 3;
			printf("ROR 0x%02X%02X\n", ram[pc + 1], ram[pc + 2]);
			break;

		case 0x6E: // DBNZ
			bytes = 3;
			printf("DBNZ 0x%02X,0x%02X\n", ram[pc + 1], ram[pc + 2]);
			break;

		default:
			printf("Unknown: %02X\n", opcode);
	}
*/

	return(op->bytes);
}

spc_file_t *read_spc_file(char *filename)
{
	FILE *f;
	Uint8 buf[1024];
	int err;
	int x;
	Uint8 *ptr;

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
		perror("spc_read_file: fread(header)");
		free(spc);
		return(NULL);
	}

	memcpy(spc->header, buf, SPC_HEADER_LEN);
	spc->header[SPC_HEADER_LEN + 1] = '\0';

	printf("Header: [%s]\n", spc->header);

	ptr = &buf[SPC_VERSION_OFFSET];
	spc->version_minor = *ptr;

	printf("Version minor: [%d]\n", spc->version_minor);

	ptr++;

	memcpy(&spc->registers.pc, ptr, 2);
	// spc->registers.pc = ntohs(spc->registers.pc);

	ptr += 2;

	spc->registers.a = *ptr++;
	spc->registers.x = *ptr++;
	spc->registers.y = *ptr++;
	memcpy(&spc->registers.psw, ptr++, 1);
	//spc->registers.psw = *ptr++;
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

	return(spc);
}

void usage(char *argv0)
{
	printf("Usage: %s <filename.spc>\n", argv0);
}

int main (int argc, char *argv[])
{
	spc_file_t *spc_file;
	Uint8 byte;
	spc_state_t state;

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

	dump_registers(state.regs);

	byte = state.ram[state.regs->pc];

	int x;
	int inc = 0;
	for (x = 0; x < 10; x++) {
		inc += dump_instruction(state.regs->pc + inc, state.ram);
	}

	do_beq(&state, state.ram[state.regs->pc + 1]);
	dump_registers(state.regs);

	dump_instruction(state.regs->pc, state.ram);

	return (0);
}
