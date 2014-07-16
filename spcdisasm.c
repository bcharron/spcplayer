/*
 * spc-disasm.c - <description>
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
 * spc-disasm.c - Benjamin Charron <bcharron@pobox.com>
 * Created  : Wed Sep  7 21:24:38 2011
 * Revision : $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include "opcodes.h"

void usage(char *argv0)
{
	printf("Usage: %s <filename.spc>\n", argv0);
}

int main (int argc, char *argv[])
{
	FILE *f;
	char *filename;
	unsigned char buf[16];
	int len;
	int pos;
	int start_offset = 0;

	if (argc < 2 || argc > 3) {
		usage(argv[0]);
		exit(1);
	}

	filename = argv[1];
	
	if (argc == 3) {
		start_offset = atoi(argv[2]);
	}
	
	f = fopen(filename, "r");
	if (! f) {
		perror("fopen()");
		exit(1);
	}

	/* Skip header */
	fseek(f, 0x100, SEEK_SET);

	pos = 0;

	if (start_offset > 0) {
		pos = start_offset;
		fseek(f, 0x100 + start_offset, SEEK_SET);
	}

	while (! feof(f) && pos <= 0xFFFF) {
		len = fread(buf, 1, 1, f);
		if (len != 1) {
			printf("End of file.\n");
			break;
		}

		int x;
		unsigned char opcode;

		opcode = buf[0];
		opcode_t *opcode_ptr = NULL;

		for (x = 0; x < OPCODE_TABLE_LEN; x++) {
			if (OPCODE_TABLE[x].opcode == opcode) {
				opcode_ptr = &OPCODE_TABLE[x];
				break;
			}
		}

		if (opcode_ptr) {
			len = fread(buf + 1, 1, opcode_ptr->len - 1, f);
			if (len != opcode_ptr->len - 1) {
				printf("Opcode on boundary: 0x%02X\n", opcode);
				break;
			}

			char str[128];
			str[0] = '\0';

			switch(opcode_ptr->len) {
				case 1:
				{
					snprintf(str, sizeof(str), "%s", opcode_ptr->name);
					break;
				}

				case 2:
				{
					snprintf(str, sizeof(str), opcode_ptr->name, buf[1]);
					break;
				}

				case 3:
				{
					/* XXX: This disassembly isn't always
					 * correct. ie, "8F AA F4" is
					 * disassembled as "MOV $AA,#$F4"
					 * instead of "MOV $F4,#$AA" */
					snprintf(str, sizeof(str), opcode_ptr->name, buf[1], buf[2]);
					break;
				}

				default:
				{
					fprintf(stderr, "Error: Instruction 0x%02X has %d bytes. Expected between 1 and 3.\n", opcode, opcode_ptr->len);
					break;
				}
			}

			printf("$%04X   ", pos);
			for (x = 0; x < opcode_ptr->len; x++)
				printf("%02X ", buf[x]);

			for (x = 0; x < 3 - opcode_ptr->len; x++)
				printf("   ");

			printf("%s\n", str);
		}

		if (opcode_ptr)
			pos += opcode_ptr->len;
		else
			pos++;
	}

	fclose(f);

	return (0);
}
