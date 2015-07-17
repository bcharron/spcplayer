#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "opcodes.h"

opcode_t *convert_opcode_table(void) {
	opcode_t *table;

	table = malloc(sizeof(opcode_t) * 256);
	if (NULL == table) {
		perror("convert_opcode_table() -> malloc()");
		exit(1);
	}

	memset(table, 0, sizeof(opcode_t) * 256);

	for (int x = 0; x < OPCODE_TABLE_LEN; x++) {
		int op = OPCODE_TABLE[x].opcode;

		table[op].opcode = OPCODE_TABLE[x].opcode;
		table[op].name = OPCODE_TABLE[x].name;
		table[op].len = OPCODE_TABLE[x].len;

		assert(table[op].len > 0);
	}

	return(table);
}

int main(int argc, char *argv[]) {
	opcode_t *table = convert_opcode_table();

	for (int x = 0; x < 256; x++) {
		if (strlen(table[x].name) == 0) {
			printf("\t/* 0x%02X */  { \"%s\", 0x%02X, %d },\n", x, "INVALID", x, 0);
		} else {
			printf("\t/* 0x%02X */  { \"%s\", 0x%02X, %d },\n", x, table[x].name, x, table[x].len);
		}
	}

	return(0);
}
