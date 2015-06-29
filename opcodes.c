#include "opcodes.h"

opcode_t OPCODE_TABLE[] = {
	{ "ADC A,(X)", 0x86, 1 },
	{ "ADC A,[$%02X+X]", 0x87, 2 },
	{ "ADC A,#$%02X", 0x88, 2 },
	{ "ADC A,$%02X+X", 0x95, 3 },
	{ "ADCZ A,$%02X+X", 0x94, 2 },
	{ "ADC A,$%02X%02X+Y", 0x96, 3 },
	{ "ADC A,[$%02X]+Y", 0x97, 2 },
	{ "ADC A,$%02X%02X", 0x85, 3 },
	{ "ADCZ A,$%02X", 0x84, 2 },
	{ "ADC $%02X,#$%02X", 0x98, 3 },
	{ "ADC $%02X,$%02X", 0x89, 3 },
	{ "AND A,(X)", 0x26, 1 },
	{ "AND A,[$%02X+X]", 0x27, 2 },
	{ "AND A,#$%02X", 0x28, 2 },
	{ "AND A,$%02X%02X+X", 0x35, 3 },
	{ "ANDZ A,$%02X+X", 0x34, 2 },
	{ "AND A,$%02X%02X+Y", 0x36, 3 },
	{ "AND A,[$%02X]+Y", 0x37, 2 },
	{ "AND A,$%02X%02X", 0x25, 3 },
	{ "ANDZ A,$%02X", 0x24, 2 },
	{ "AND (X),(Y)", 0x39, 1 },
	{ "AND $%02X,#$%02X", 0x38, 3 },
	{ "AND $%02X,$%02X", 0x29, 3 },
	{ "AND1 C,$%02X", 0x4A, 3 },
	{ "AND1 C,/$%02X", 0x6A, 3 },
	{ "ASL A", 0x1C, 1 },
	{ "ASL $%02X,X", 0x1B, 2 },
	{ "ASL $%02X%02X", 0x0C, 3 },
	{ "ASLZ $%02X", 0x0B, 2 },
	{ "LSR A", 0x5C, 1 },
	{ "LSR $%02X,X", 0x5B, 2 },
	{ "LSR $%02X", 0x4C, 3 },
	{ "LSRZ $%02X", 0x4B, 2 },
	{ "ROL A", 0x3C, 1 },
	{ "ROL $%02X,X", 0x3B, 2 },
	{ "ROL $%02X", 0x2C, 3 },
	{ "ROLZ $%02X", 0x2B, 2 },
	{ "ROR A", 0x7C, 1 },
	{ "ROR $%02X,X", 0x7B, 2 },
	{ "ROR $%02X", 0x6C, 3 },
	{ "RORZ $%02X", 0x6B, 2 },
	{ "BBC0 $%02X,$%02X", 0x13, 3 },
	{ "BBC1 $%02X,$%02X", 0x33, 3 },
	{ "BBC2 $%02X,$%02X", 0x53, 3 },
	{ "BBC3 $%02X,$%02X", 0x73, 3 },
	{ "BBC4 $%02X,$%02X", 0x93, 3 },
	{ "BBC5 $%02X,$%02X", 0xB3, 3 },
	{ "BBC6 $%02X,$%02X", 0xD3, 3 },
	{ "BBC7 $%02X,$%02X", 0xF3, 3 },
	{ "BBS0 $%02X,$%02X", 0x03, 3 },
	{ "BBS1 $%02X,$%02X", 0x23, 3 },
	{ "BBS2 $%02X,$%02X", 0x43, 3 },
	{ "BBS3 $%02X,$%02X", 0x63, 3 },
	{ "BBS4 $%02X,$%02X", 0x83, 3 },
	{ "BBS5 $%02X,$%02X", 0xA3, 3 },
	{ "BBS6 $%02X,$%02X", 0xC3, 3 },
	{ "BBS7 $%02X,$%02X", 0xE3, 3 },
	{ "BPL $%04X", 0x10, 2 },
	{ "BRA $%04X", 0x2F, 2 },
	{ "BMI $%04X", 0x30, 2 },
	{ "BVC $%04X", 0x50, 2 },
	{ "BVS $%04X", 0x70, 2 },
	{ "BCC $%04X", 0x90, 2 },
	{ "BCS $%04X", 0xB0, 2 },
	{ "BNE $%04X", 0xD0, 2 },
	{ "BEQ $%04X", 0xF0, 2 },
	{ "SET0 $%02X", 0x02, 2 },
	{ "SET1 $%02X", 0x22, 2 },
	{ "SET2 $%02X", 0x42, 2 },
	{ "SET3 $%02X", 0x62, 2 },
	{ "SET4 $%02X", 0x82, 2 },
	{ "SET5 $%02X", 0xA2, 2 },
	{ "SET6 $%02X", 0xC2, 2 },
	{ "SET7 $%02X", 0xE2, 2 },
	{ "CLR0 $%02X", 0x12, 2 },
	{ "CLR1 $%02X", 0x32, 2 },
	{ "CLR2 $%02X", 0x52, 2 },
	{ "CLR3 $%02X", 0x72, 2 },
	{ "CLR4 $%02X", 0x92, 2 },
	{ "CLR5 $%02X", 0xB2, 2 },
	{ "CLR6 $%02X", 0xD2, 2 },
	{ "CLR7 $%02X", 0xF2, 2 },
	{ "CMP A,(X)", 0x66, 1 },
	{ "CMP A,[$%02X+X]", 0x67, 2 },
	{ "CMP A,#$%02X", 0x68, 2 },
	{ "CMP A,$%02X+X", 0x75, 3 },
	{ "CMPZ A,$%02X+X", 0x74, 2 },
	{ "CMP A,$%02X+Y", 0x76, 3 },
	{ "CMP A,[$%02X]+Y", 0x77, 2 },
	{ "CMP A,$%02X%02X", 0x65, 3 },
	{ "CMPZ A,$%02X", 0x64, 2 },
	{ "CMP X,#$%02X", 0xC8, 2 },
	{ "CMP X,$%02X", 0x1E, 3 },
	{ "CMP X,$%02X", 0x3E, 2 },
	{ "CMP Y,#$%02X", 0xAD, 2 },
	{ "CMP Y,$%02X%02X", 0x5E, 3 },
	{ "CMP Y,$%02X", 0x7E, 2 },
	{ "CMP (X),(Y)", 0x79, 1 },
	{ "CMP $%02X,#$%02X", 0x78, 3 },
	{ "CMP $%02X,$%02X", 0x69, 3 },
	{ "CBNE $%02X+X,$%02X", 0xDE, 3 },
	{ "CBNE $%02X,$%02X", 0x2E, 3 },
	{ "DBNZ Y,$%02X", 0xFE, 2 },
	{ "DBNZ $%02X, #$%02X", 0x6E, 3 },
	{ "DAA YA", 0xDF, 1 },
	{ "DAS YA", 0xBE, 1 },
	{ "NOT1 $%02X", 0xEA, 3 },
	{ "XCN A", 0x9F, 1 },
	{ "MOV1 C,$%02X", 0xAA, 3 },
	{ "MOV1 $%02X,C", 0xCA, 3 },
	{ "DECW $%02X", 0x1A, 2 },
	{ "INCW $%02X", 0x3A, 2 },
	{ "CLRW $%02X", 0x5A, 2 },
	{ "ADDW YA,$%02X", 0x7A, 2 },
	{ "SUBW YA,$%02X", 0x9A, 2 },
	{ "MOVW YA,$%02X", 0xBA, 2 },
	{ "MOVW $%02X,YA", 0xDA, 2 },
	{ "MUL YA", 0xCF, 1 },
	{ "DIV YA,X", 0x9E, 1 },
	{ "EOR A,(X)", 0x46, 1 },
	{ "EOR A,[$%02X+X]", 0x47, 2 },
	{ "EOR A,#$%02X", 0x48, 2 },
	{ "EOR A,$%02X+X", 0x55, 3 },
	{ "EORZ A,$%02X+X", 0x54, 2 },
	{ "EOR A,$%02X+Y", 0x56, 3 },
	{ "EOR A,[$%02X]+Y", 0x57, 2 },
	{ "EOR A,$%02X", 0x45, 3 },
	{ "EORZ A,$%02X", 0x44, 2 },
	{ "EOR (X),(Y)", 0x59, 1 },
	{ "EOR $%02X,#$%02X", 0x58, 3 },
	{ "EOR $%02X,$%02X", 0x49, 3 },
	{ "EOR1 C,$%02X", 0x8A, 3 },
	{ "DEC A", 0x9C, 1 },
	{ "DEC X", 0x1D, 1 },
	{ "DEC Y", 0xDC, 1 },
	{ "DEC $%02X+X", 0x9B, 2 },
	{ "DEC $%02X", 0x8C, 3 },
	{ "DECZ $%02X", 0x8B, 2 },
	{ "INC A", 0xBC, 1 },
	{ "INC X", 0x3D, 1 },
	{ "INC Y", 0xFC, 1 },
	{ "INC $%02X+X", 0xBB, 2 },
	{ "INC $%02X%02X", 0xAC, 3 },
	{ "INCZ $%02X", 0xAB, 2 },
	{ "MOV X,A", 0x5D, 1 },
	{ "MOV A,X", 0x7D, 1 },
	{ "MOV X,SP", 0x9D, 1 },
	{ "MOV SP,X", 0xBD, 1 },
	{ "MOV A,Y", 0xDD, 1 },
	{ "MOV Y,A", 0xFD, 1 },
	{ "MOV (X),(Y)", 0x99, 1 },
	{ "MOV (X)+,A", 0xAF, 1 },
	{ "MOV A,(X)+", 0xBF, 1 },
	{ "MOV (X),A", 0xC6, 1 },
	{ "MOV A,(X)", 0xE6, 1 },
	{ "MOV Y,#$%02X", 0x8D, 2 },
	{ "MOV X,#$%02X", 0xCD, 2 },
	{ "MOV A,#$%02X", 0xE8, 2 },
	{ "MOV [$%02X+X],A", 0xC7, 2 },
	{ "MOV [$%02X]+Y,A", 0xD7, 2 },
	{ "MOV A,[$%02X+X]", 0xE7, 2 },
	{ "MOV A,[$%02X]+Y", 0xF7, 2 },
	{ "MOV $%02X%02X+X,A", 0xD5, 3 },
	{ "MOVZ $%02X+X,A", 0xD4, 2 },
	{ "MOV $%02X+Y,A", 0xD6, 3 },
	{ "MOV $%02X+Y,X", 0xD9, 2 },
	{ "MOV $%02X+X,Y", 0xDB, 2 },
	{ "MOV X,$%02X+Y", 0xF9, 2 },
	{ "MOV Y,$%02X+X", 0xFB, 2 },
	{ "MOV A,$%02X%02X+X", 0xF5, 3 },
	{ "MOVZ A,$%02X+X", 0xF4, 2 },
	{ "MOV A,$%02X%02X+Y", 0xF6, 3 },
	{ "MOV $%02X%02X,A", 0xC5, 3 },
	{ "MOVZ $%02X,A", 0xC4, 2 },
	{ "MOV $%02X%02X,X", 0xC9, 3 },
	{ "MOV $%02X,X", 0xD8, 2 },
	{ "MOV $%02X%02X,Y", 0xCC, 3 },
	{ "MOV $%02X,Y", 0xCB, 2 },
	{ "MOV A,$%02X%02X", 0xE5, 3 },
	{ "MOVZ A,$%02X", 0xE4, 2 },
	{ "MOV X,$%02X%02X", 0xE9, 3 },
	{ "MOV X,$%02X", 0xF8, 2 },
	{ "MOV Y,$%02X%02X", 0xEC, 3 },
	{ "MOV Y,$%02X", 0xEB, 2 },
	{ "MOV $%02X,#$%02X", 0x8F, 3 },
	{ "MOV $%02X,$%02X", 0xFA, 3 },
	{ "OR A,(X)", 0x06, 1 },
	{ "OR A,[$%02X+X]", 0x07, 2 },
	{ "OR A,#$%02X", 0x08, 2 },
	{ "OR A,$%02X+X", 0x15, 3 },
	{ "ORZ A,$%02X+X", 0x14, 2 },
	{ "OR A,$%02X+Y", 0x16, 3 },
	{ "OR A,[$%02X]+Y", 0x17, 2 },
	{ "OR A,$%02X%02X", 0x05, 3 },
	{ "ORZ A,$%02X", 0x04, 2 },
	{ "OR (X),(Y)", 0x19, 1 },
	{ "OR $%02X,#$%02X", 0x18, 3 },
	{ "OR $%02X,$%02X", 0x09, 3 },
	{ "OR1 C,$%02X", 0x0A, 3 },
	{ "OR1 C,/$%02X", 0x2A, 3 },
	{ "SBC A,(X)", 0xA6, 1 },
	{ "SBC A,[$%02X+X]", 0xA7, 2 },
	{ "SBC A,#$%02X", 0xA8, 2 },
	{ "SBC A,$%02X+X", 0xB5, 3 },
	{ "SBCZ A,$%02X+X", 0xB4, 2 },
	{ "SBC A,$%02X+Y", 0xB6, 3 },
	{ "SBC A,[$%02X]+Y", 0xB7, 2 },
	{ "SBC A,$%02X", 0xA5, 3 },
	{ "SBCZ A,$%02X", 0xA4, 2 },
	{ "SBC (X),(Y)", 0xB9, 1 },
	{ "SBC $%02X,#$%02X", 0xB8, 3 },
	{ "SBC $%02X,$%02X", 0xA9, 3 },
	{ "TCALL 0 [$FFDE]", 0x01, 1 },
	{ "TCALL 1 [$FFDC]", 0x11, 1 },
	{ "TCALL 2 [$FFDA]", 0x21, 1 },
	{ "TCALL 3 [$FFD8]", 0x31, 1 },
	{ "TCALL 4 [$FFD6]", 0x41, 1 },
	{ "TCALL 5 [$FFD4]", 0x51, 1 },
	{ "TCALL 6 [$FFD2]", 0x61, 1 },
	{ "TCALL 7 [$FFD0]", 0x71, 1 },
	{ "TCALL 8 [$FFCE]", 0x81, 1 },
	{ "TCALL 9 [$FFCC]", 0x91, 1 },
	{ "TCALL 10 [$FFCA]", 0xA1, 1 },
	{ "TCALL 11 [$FFC8]", 0xB1, 1 },
	{ "TCALL 12 [$FFC6]", 0xC1, 1 },
	{ "TCALL 13 [$FFC4]", 0xD1, 1 },
	{ "TCALL 14 [$FFC2]", 0xE1, 1 },
	{ "TCALL 15 [$FFC0]", 0xF1, 1 },
	{ "TSET1 $%02X%02X", 0x0E, 3 },
	{ "TCLR1 $%02X", 0x4E, 3 },
	{ "CALL $%02X%02X", 0x3F, 3 },
	{ "PCALL $%02X", 0x4F, 2 },
	{ "JMP [$%02X%02X+X]", 0x1F, 3 },
	{ "JMP $%02X%02X", 0x5F, 3 },
	{ "PUSH PSW", 0x0D, 1 },
	{ "PUSH A", 0x2D, 1 },
	{ "PUSH X", 0x4D, 1 },
	{ "PUSH Y", 0x6D, 1 },
	{ "POP PSW", 0x8E, 1 },
	{ "POP A", 0xAE, 1 },
	{ "POP X", 0xCE, 1 },
	{ "POP Y", 0xEE, 1 },
	{ "NOP", 0x00, 1 },
	{ "BRK """, 0x0F, 1 },
	{ "RET """, 0x6F, 1 },
	{ "RETI """, 0x7F, 1 },
	{ "CLRP """, 0x20, 1 },
	{ "SETP """, 0x40, 1 },
	{ "CLRC """, 0x60, 1 },
	{ "SETC """, 0x80, 1 },
	{ "EI """, 0xA0, 1 },
	{ "DI """, 0xC0, 1 },
	{ "CLRV """, 0xE0, 1 },
	{ "NOTC """, 0xED, 1 },
	{ "SLEEP """, 0xEF, 1 },
	{ "STOP """, 0xFF, 1 },
};

int OPCODE_TABLE_LEN = (sizeof(OPCODE_TABLE) / sizeof(opcode_t));
