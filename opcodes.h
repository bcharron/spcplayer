#ifndef _OPCODES_H
#define _OPCODES_H

typedef struct opcode_s {
	char *name;
	int opcode;
	int len;
} opcode_t;

extern opcode_t OPCODE_TABLE[];
extern int OPCODE_TABLE_LEN;

// #define OPCODE_TABLE_LEN (sizeof(OPCODE_TABLE) / sizeof(opcode_t))

#endif
