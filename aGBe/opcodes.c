#include "main.h"
#include "opcodes.h"

BYTE INCreg(BYTE reg){
	int a = reg + 1; //reg = INC(reg);
	//setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)a & 0xFF;
}

BYTE DECreg(BYTE reg){
	int a = reg - 1;
	//reg -= 1 ;  DEC(reg);
	//setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(1);
	return (BYTE)(a & 0xFF);
}

WORD INCWreg(WORD reg){
	reg += 1;
	reg &= 0xFFFF;
	if (reg==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (WORD)reg;
}

WORD DECWreg(WORD reg){
	reg -= 1;
	reg &= 0xFFFF;
	if (reg==0) { setZ(1); } else { setZ(0); }
	setN(1);
	return (WORD)reg;
}


BYTE ADDreg(BYTE regA, BYTE regB){
	int a = regA + regB;
	setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)a & 0xFF;
}

BYTE ADCreg(BYTE regA, BYTE regB){
	int a = regA + regB + getC(); //0x00;//= ADC(regA);
	setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)a & 0xFF;
}

BYTE SUBreg(BYTE regA, BYTE regB){
	int a = regA - regB;
	setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(1);
	return (BYTE)a & 0xFF;
}

BYTE SBCreg(BYTE regA, BYTE regB){
	int a = regA - regB - getC();  /// get_rH()
	setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(1);
	return (BYTE)a & 0xFF;
}

BYTE ANDreg(BYTE regA, BYTE regB){
	int a = regA & regB;
	setC(0);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(1);
	return (BYTE)a;
}

BYTE ORreg(BYTE regA, BYTE regB){
    int a;
    a = regA | regB;
    setC(0);
    if (a==0) { setZ(1); } else { setZ(0); }
    setN(0);
	return (BYTE)a;
}

BYTE XORreg(BYTE regA, BYTE regB){
	int a;
	a = (regA ^ regB) & 0xFF;
	setC(0);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)a;
}

void CPreg(BYTE regA, BYTE regB) {
	setC(regA < regB);
	if (regA==regB) { setZ(1); } else { setZ(0); }
	setN(1);
}

BYTE CPLreg(BYTE regA) {
	int a = ~regA;
	setN(1);
	setH(1);
	return (BYTE)a;
}

WORD jr(WORD reg, BYTE off) {
	reg += (signed char)off; // test
	reg &= 0xFFFF;
	return reg;
}

WORD jp(WORD reg) {
	reg &= 0xFFFF;
	return reg;
}
WORD ADDWreg(WORD regA, WORD regB) {
	int aW = regA + regB;
	setC(aW != (aW & 0xFFFF));
	aW &= 0xFFFF;
	return (WORD)aW;
}
BYTE RLA(BYTE reg){
	reg = RL(reg);
	setZ(0);
	reg &= 0xFF;
	return reg;
}
BYTE RRA(BYTE reg){
	reg = RR(reg);
	setZ(0);
	reg &= 0xFF;
	return reg;
}
BYTE RLC(BYTE reg) {
	int a;
	a = (reg << 1) & 0xFF | ((reg) >> 7);
	setC((reg >> 7) & 0x01);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(0);
	return (BYTE)a & 0xFF;
}
BYTE RL(BYTE reg) {
	int a;
	a = (reg << 1) & 0xFF | (BYTE)getC() & 0x01;
	setC(reg >> 7);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(0);
	return (BYTE)a & 0xFF;
}
BYTE RRC(BYTE reg) {
	int a;
	setC(reg & 0x01);
	a = (reg >> 1) | ((reg & 0x01) << 7);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(0);
	return (BYTE)a & 0xFF;
}
BYTE RR(BYTE reg) {
	int a;
	a = (reg >> 1) | (BYTE)(getC() & 0x01) << 7;
	setC((reg & 0x01));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(0);
	return (BYTE)a & 0xFF;
}
BYTE SLA(BYTE reg) {
	int a = (reg << 1);
	setC( a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)a & 0xFF;
}
BYTE SRA(BYTE reg) {
	int a = (reg >> 1) | (reg & 0x80);
	setC(reg & 0x01);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(0);
	return (BYTE)a & 0xFF;
}
BYTE SLL(BYTE reg) {
	int a = ((reg << 4) & 0xF0) | (reg >> 4);
	setC(0);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(0);
	return (BYTE)a & 0xFF;
}

BYTE SRL(BYTE reg) {
	setC(reg & 0x01);
	reg = reg >> 1;
	if (reg==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return reg & 0xFF;
}
BYTE SET(int i, BYTE reg){
	reg |= (0x01 << i);
	return reg & 0xFF;
}
BYTE RES(int i, BYTE reg){
	BYTE res;
	res = ~((0x01 << i) & 0xFF);
	res &= reg;
 	return res & 0xFF;
}

void BIT(int i, BYTE reg){
	BYTE res;
	res = (0x01 << i) & reg;
	res &= 0xFF;
	if (res==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(1);
}

