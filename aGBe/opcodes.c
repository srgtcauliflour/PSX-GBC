#include "main.h"
#include "opcodes.h"
// Previously missing: every flag setter/getter (setZ/setN/setH/setC/getZ...)
// and ReadMEM/WriteMEM used below were only ever implicitly declared. Lenient
// old compilers (and GCC with warnings suppressed) tolerate that, but it's
// undefined behaviour, and a modern strict cross-compiler for the real PSX
// port is liable to either hard-error on it or -- worse -- silently
// miscompile a call whose real signature doesn't match the assumed
// "implicit int" one.
#include "emu.h"

BYTE INCreg(BYTE reg){
	// BUG FIX: Z was previously tested against the pre-mask int value, which can
	// never be 0 for an 8-bit INC (0xFF+1=256, not 0) - so Z never got set on the
	// 0xFF->0x00 wraparound. Any code using the extremely common "INC r / JR Z or
	// NZ" pattern to run a 256-iteration loop (checksum/copy/delay loops, etc.)
	// would spin forever. Masking before the comparison is the actual fix.
	// Also restores the H flag, which this never set at all: real hardware sets
	// H when the low nibble overflows (i.e. it was 0xF before the increment).
	int a = reg + 1;
	setH((reg & 0x0F) == 0x0F);
	if ((a & 0xFF) == 0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)(a & 0xFF);
}

BYTE DECreg(BYTE reg){
	// Same class of fix as INCreg for consistency/H flag; the Z check here
	// happened to already be correct since reg-1 can only be negative (never
	// exactly 0) when it *shouldn't* set Z, but is corrected to compare the
	// masked result on principle, matching hardware behaviour exactly.
	int a = reg - 1;
	setH((reg & 0x0F) == 0x00);
	if ((a & 0xFF) == 0) { setZ(1); } else { setZ(0); }
	setN(1);
	return (BYTE)(a & 0xFF);
}

WORD INCWreg(WORD reg){
	// BUG FIX: real Game Boy hardware's 16-bit INC (INC BC/DE/HL/SP) affects NO
	// flags at all. This was incorrectly setting Z/N, corrupting flags that the
	// following instruction may depend on (e.g. code doing "INC HL" purely for
	// pointer arithmetic between a comparison and its conditional jump).
	reg += 1;
	reg &= 0xFFFF;
	return (WORD)reg;
}

WORD DECWreg(WORD reg){
	// Same fix as INCWreg: 16-bit DEC (DEC BC/DE/HL/SP) affects no flags either.
	reg -= 1;
	reg &= 0xFFFF;
	return (WORD)reg;
}


BYTE ADDreg(BYTE regA, BYTE regB){
	// BUG FIX: H (half-carry, carry out of bit 3) was never set at all.
	int a = regA + regB;
	setH(((regA & 0x0F) + (regB & 0x0F)) > 0x0F);
	setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)a & 0xFF;
}

BYTE ADCreg(BYTE regA, BYTE regB){
	// BUG FIX: H never set; also has to include the incoming carry bit in the
	// half-carry calculation, not just regA/regB.
	int carryIn = getC();
	int a = regA + regB + carryIn;
	setH(((regA & 0x0F) + (regB & 0x0F) + carryIn) > 0x0F);
	setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	return (BYTE)a & 0xFF;
}

BYTE SUBreg(BYTE regA, BYTE regB){
	// BUG FIX: H (borrow out of bit 4) was never set at all.
	int a = regA - regB;
	setH((regA & 0x0F) < (regB & 0x0F));
	setC(a != (a & 0xFF));
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(1);
	return (BYTE)a & 0xFF;
}

BYTE SBCreg(BYTE regA, BYTE regB){
	// BUG FIX: H never set; must include the incoming carry/borrow bit.
	int carryIn = getC();
	int a = regA - regB - carryIn;
	setH((int)(regA & 0x0F) - (int)(regB & 0x0F) - carryIn < 0);
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
    // BUG FIX: OR always clears H on real hardware; this never touched it,
    // silently leaking whatever H happened to be left over from a prior op.
    int a;
    a = regA | regB;
    setC(0);
    if (a==0) { setZ(1); } else { setZ(0); }
    setN(0);
    setH(0);
	return (BYTE)a;
}

BYTE XORreg(BYTE regA, BYTE regB){
	// BUG FIX: same H-not-cleared gap as ORreg above.
	int a;
	a = (regA ^ regB) & 0xFF;
	setC(0);
	if (a==0) { setZ(1); } else { setZ(0); }
	setN(0);
	setH(0);
	return (BYTE)a;
}

void CPreg(BYTE regA, BYTE regB) {
	// BUG FIX: CP is a SUB that discards its result but still sets flags the
	// same way SUB does - H was never set here at all.
	setH((regA & 0x0F) < (regB & 0x0F));
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
	// BUG FIX: never set H, never cleared N. Real hardware: ADD HL,rr clears
	// N, sets H from a carry out of bit 11 (i.e. the low 12 bits overflow),
	// sets C from a carry out of bit 15, and leaves Z alone entirely.
	int aW = regA + regB;
	setH(((regA & 0x0FFF) + (regB & 0x0FFF)) > 0x0FFF);
	setC(aW != (aW & 0xFFFF));
	setN(0);
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

