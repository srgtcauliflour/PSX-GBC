// aGBe
// EMU.C
// TODO: Write File Description

// includes ////////////////////////////////////////////////////
#include <stdio.h>
#include <kernel.h>
#include <sys/types.h>
#include <sys/file.h>
#include <malloc.h>
#include <memory.h>
#include <libetc.h>
#include <libgte.h>
#include <libgpu.h>
#include <libgs.h>
#include <libcd.h>
#include <libapi.h>
#include "main.h"
//#include "mem.h"
#include "emu.h"
#include "psx.h"
#include "pad.h"

// defines ////////////////////////////////////////////////////
#define HBLANKMODE 0 // 00: Entire Display Ram can be accessed
#define VBLANKMODE 1 // 01: During V-Blank
#define OAMMODE 2    // 10: During Searching OAM-RAM
#define TRANSFERMODE 3 // 11: During Transfering Data to LCD Driver
#define VBLANK_CYCLES   109
#define HBLANK_CYCLES    49
#define OAM_CYCLES       20
#define TRANSFER_CYCLES  40


// globals ////////////////////////////////////////////////////
int *fp;
BYTE currOp;
BYTE reg_A, reg_B, reg_C, reg_D, reg_E, reg_F;
WORD reg_HL, reg_SP, reg_PC;
BYTE IER;
BYTE IFLAG;
int videoMode = 0;
int curframe = 0;
int frameskip = 2;
int IME;
int EMULATING;
BYTE *VRAM, *EXTRNRAM, *RAM, *OAMRAM, *HIRAM;
int temp;
int illegalOpcodes = 0, totalOpcodes = 0, devOpcodes = 0;
int t, n,z, i, j;
int pauseMnuPos = 0;
double CLOCKSPEED = 4.123;

//char buff[200];
int screenBuffer[160*144];
int runto = 0;

int Voff;
int cyclesLeft = 0;
// Video
int VideoCyclesLeft;
BYTE SCRX, SCRY;
BYTE WNDX, WNDY;
BYTE LCDCONTROL;
BYTE LCDSTATUS;
BYTE LCDY, LYC;
BYTE OBJPAL0, OBJPAL1;
BYTE BGPAL;
BYTE P1;
BYTE TIMEMOD, TIMCONT;
int TIMECNT;
int MAXTIME, TIMECOUNTER;
int ROMBANKNUMBER = 0;// For MBC1 and MBC2
int RAMBANKNUMBER = 0;
int MBCMODE = 0;
//char pauseData[6][10]={ "Continue", "Save", "Load", "Reset", "Options", "Exit" };

BYTE ROMSIZE, CARTTYPE, RAMSIZE, VERSIONNUMBER;
int iROMSIZE, iRAMSIZE;
BYTE CARTTITLE[16];
BYTE MANUCODE[2];


// functions ////////////////////////////////////////////////////
void reset_Z80() {
		#if defined(DEBUG)
		printf("Reseting Z80 Core..\n");
		#endif
		Allocate_Memory();
		EMULATING =  1;
		ROMBANKNUMBER = 0;
		RAMBANKNUMBER = 0;
		// Set Registers
		reg_HL= 0x014D;
		reg_SP= 0xFFFE;
		reg_PC= 0x0100;
		reg_A = 0x01;
		reg_B = 0x00;
		reg_C = 0x13;
		reg_D = 0x00;
		reg_E = 0xD8;
		reg_F = 0xB0;
		IFLAG = 0x01;
		IER = 0x00;
		IME = 0x00;
		SCRX = 0x00;
		SCRY = 0x00;
		LCDY = 0x00;
		LYC = 0x00;
		VideoCyclesLeft = 100;
		MAXTIME = 1024;
		TIMECOUNTER = 0;
		TIMECNT = 0;
		TIMCONT = 0;
		MBCMODE = 0;
}

WORD get_rAF(void) {
	return (WORD)((reg_A << 8) | reg_F);
}
WORD get_rBC(void) {
	return (WORD)((reg_B << 8) | reg_C);
}
WORD get_rDE(void) {
	return (WORD)((reg_D << 8) | reg_E);
}
BYTE get_rH(void) {
	return (BYTE)((reg_HL >> 8) & 0xFF);
}
BYTE get_rL(void) {
	return (BYTE)(reg_HL & 0xFF);
}
void put_rAF(WORD r1) {
	reg_A = (r1 >> 8) & 0xFF;
	reg_F = r1 & 0xFF;
}
void put_rBC(WORD r1) {
	reg_B = (r1 >> 8) & 0xFF;
	reg_C = r1 & 0xFF;
}
void put_rDE(WORD r1) {
	reg_D = (r1 >> 8) & 0xFF;
	reg_E = r1 & 0xFF;
}
void put_rH(BYTE r1) {
	reg_HL = (WORD)((reg_HL & 0x00FF) | ((r1 << 8) & 0xFF00));
}
void put_rL(BYTE r1) {
	reg_HL = (WORD)((reg_HL & 0xFF00) | (r1 & 0x00FF));
}
int getZ(void) {
	return (reg_F & Z_FLAG) == Z_FLAG;
}
int getN(void) {
	return (reg_F & N_FLAG) == N_FLAG;
}
int getH(void) {
	return (reg_F & H_FLAG) == H_FLAG;
}
int getC(void) {
	return (reg_F & C_FLAG) == C_FLAG;
}
void setZ(int flag) {
	reg_F &= ~Z_FLAG;
	if (flag) {
		reg_F |= Z_FLAG;
    }
}
void setN(int flag) {
	reg_F &= ~N_FLAG;
	if (flag) {
		reg_F |= N_FLAG;
	}
}
void setH(int flag) {
	reg_F &= ~H_FLAG;
	if (flag) {
		reg_F |= H_FLAG;
	}
}
void setC(int flag) {
	reg_F &= ~C_FLAG;
	if (flag) {
		reg_F |= C_FLAG;
	}
}
void push(WORD wVal){
	WriteMEM(--reg_SP, (wVal >> 8) & 0xFF);
	WriteMEM(--reg_SP, wVal & 0xFF);
}
WORD pop(void){
	WORD aW = ReadWord(reg_SP); //((ReadMEM(reg_SP) | ReadMEM(reg_SP+1) << 8) & 0xFFFF);
	reg_SP+=2;
	return (WORD)aW;
}
void call(void) {
	push(reg_PC+2);
}
WORD ret(void){
	return (WORD)pop();
}
WORD rst(WORD addr){
	push(reg_PC);
	return (WORD)addr;
}

void interrupt(void){
	if (IME && (IFLAG & IER)) {
		if 		  (IFLAG & IER & 0x01) 	      { IFLAG &= ~0x01; IME = 0; reg_PC = rst(0x0040); }  // Bit 0: V-Blank
		else if (((IFLAG & IER) >> 1) & 0x01) { IFLAG &= ~0x02; IME = 0; reg_PC = rst(0x0048); } //  Bit 1: LCD
		else if (((IFLAG & IER) >> 2) & 0x01) { IFLAG &= ~0x04; IME = 0; reg_PC = rst(0x0050); } //  Bit 2: Timer Overflow
		else if (((IFLAG & IER) >> 3) & 0x01) { IFLAG &= ~0x08; IME = 0; reg_PC = rst(0x0058); } //  Bit 3: Serial I/O transfer end
		else if (((IFLAG & IER) >> 4) & 0x01) { IFLAG &= ~0x10; IME = 0; reg_PC = rst(0x0060); } //  Bit 4: New Value on Selected Joypad Keyline(s)
	}
}

void Allocate_Memory(void){
	HIRAM  = (BYTE *)malloc(128 * sizeof(BYTE));
	VRAM   = (BYTE *)malloc(8 * 1024 * sizeof(BYTE));
	RAM    = (BYTE *)malloc(8 * 1024 * sizeof(BYTE)); //TODO: Check this out
	OAMRAM = (BYTE *)malloc(160 * sizeof(BYTE));
	EXTRNRAM = (BYTE *)malloc(iRAMSIZE * 1024 * sizeof(BYTE));
}

void UnAllocate_Memory(void){
	#if defined(DEBUG)
		printf("Unallocating Memory...\n");
	#endif

	//TODO: Check to see if allocated?

	free(HIRAM);
	free(VRAM);
	free(RAM);
	free(ROM);
	free(OAMRAM);
	free(EXTRNRAM);

}

void doDMA(BYTE addr) {
	j = (addr & 0xFF) * 0x0100;
	for (i = 0; i < 0xA0; i++) {
		WriteMEM(0xFE00 + i, ReadMEM(j + i));
	}
}

void cycleLength(int cycle) {
	if ((TIMCONT >> 2) & 0x01){
		TIMECOUNTER += cycle;
		if (TIMECOUNTER >= MAXTIME){
			TIMECOUNTER = 0;
			TIMECNT += 1;

			if (TIMECNT > 255) {
				IFLAG |= 0x04;
				TIMECNT = TIMEMOD;
				#if defined(DEBUG)
				printf("Timer Interrupt!\n");
				#endif
			}
		}
	}
	VideoCyclesLeft -= cycle;
	if(VideoCyclesLeft <= 0) { // Video
		if((videoMode == HBLANKMODE) || (videoMode == VBLANKMODE)){
			LCDY++;
			if (LCDY > 0x100){
				LCDY = 0;
			} else if (LCDY < 0x90) {
				hblank();
				videoMode = OAMMODE;
				VideoCyclesLeft = (int)(OAM_CYCLES * CLOCKSPEED);
				if ((LCDSTATUS >> 3) & 0x01) { IFLAG |= 0x02; } // LCD 3
			} else {
				videoMode = VBLANKMODE;
				VideoCyclesLeft = (int)(VBLANK_CYCLES* CLOCKSPEED);
				if (LCDY == 0x90) {
					vblank();
					if ((LCDSTATUS >> 4) & 0x01) { IFLAG |= 0x01; }
				}
			}
			if (LCDY == LYC) { IFLAG |= 0x02; } // 3
			return;
		} else {
			if (videoMode == OAMMODE) {
				videoMode = TRANSFERMODE;
				VideoCyclesLeft = (int)(TRANSFER_CYCLES * CLOCKSPEED);
				if ((LCDSTATUS >> 5) & 0x01) { IFLAG |= 0x02; } //3
				return;
			}
			if (videoMode == TRANSFERMODE) {
				videoMode = HBLANKMODE;
				VideoCyclesLeft = (int)(HBLANK_CYCLES * CLOCKSPEED);
				return;
			}
		}
  	}
}

void doCycles(){


}
int CyclesLeft(){
	cyclesLeft = 1000000; // Big Number

	if ((LCDCONTROL >> 7) == 0x01){
		if(VideoCyclesLeft < cyclesLeft) {
			cyclesLeft = VideoCyclesLeft;
		}
	}
	// Timer
	if ((TIMCONT >> 2) & 0x01){
		if ((MAXTIME - TIMECOUNTER) < cyclesLeft){
			cyclesLeft = (MAXTIME - TIMECOUNTER);
		}
	}
	return cyclesLeft;
}

void hblank(){

 	int WINaddr;
 	int BGaddr;
 	int TILEaddr;

 	if ((LCDCONTROL >> 6) & 0x01) { WINaddr = 0x9C00;  } else { WINaddr = 0x9800; }
 	if ((LCDCONTROL >> 3) & 0x01) { BGaddr = 0x9C00;   } else { BGaddr = 0x9800;}
 	if ((LCDCONTROL >> 4) & 0x01) { TILEaddr = 0x8000; } else { TILEaddr = 0x9000; }


	if ((LCDCONTROL >> 7) & 0x01) {
		if(LCDCONTROL & 0x01) {
 			DrawBGline(LCDY, BGaddr, TILEaddr);
		}

 		if((LCDCONTROL >> 5) & 0x01){
			if(!(WNDX > 166 || WNDY > LCDY)) {
				DrawWINline(LCDY, WINaddr, TILEaddr);
			}
		}
		DrawOBJline(LCDY, 0x8000);
	}
}

void DrawBGline(int line, int BGaddr, int TILEaddr) {
	int bx, by;
	int tileNo = 0;
	int colour;
	int oldtileNo = -1;
 	BYTE B1, B2;
	bx = SCRX;
	by = (SCRY + LCDY);
	for (i = 0; i < 160; i++) {
		if (BGaddr == 0x9C00) {//
			tileNo = (signed int)ReadMEM(BGaddr + (by/8) * 32 + (i+ bx)/8);
		} else {
			tileNo = (unsigned char)ReadMEM(BGaddr + (by/8) * 32 + (i+bx)/8);
		}
		if (tileNo != oldtileNo) {
	 		B1 = (unsigned char)ReadMEM(TILEaddr + (tileNo) * 16  + (by%8)*2 );
	 		B2 = (unsigned char)ReadMEM(TILEaddr + (tileNo) * 16  + (by%8)*2 + 1 );
	 		oldtileNo = tileNo;
		}
	 	if (((B1 >> (7-(i%8))) & 0x01) == 1) {
			if (((B2 >> (7-(i%8))) & 0x01) == 1) { colour = (BGPAL >> 6) & 0x3; //3;
			} else { colour = (BGPAL >> 4) & 0x3; //2;
			}
	 	} else if (((B2 >> 7-(i%8)) & 0x01) == 1) { colour = (BGPAL >> 2) & 0x3; //1;
	 	} else { colour = BGPAL & 0x3; //0;
	 	}
 		screenBuffer[(LCDY * 160) + i] = colour;
	}
}



void DrawWINline(int line, int WINaddr, int TILEaddr) {
	int tileNo = 0;
	int colour = 0;
	int bx, by;
	int oldtileNo = -1;
	BYTE B1, B2;
	int Transparency = LCDCONTROL & 0x1;
	by = LCDY - WNDY;
	for (i = 0; i < 160; i++) {
		if (WINaddr == 0x9C00) {
			tileNo = (signed int)ReadMEM(WINaddr + (by/8) * 32 + (i)/8);
		} else {
			tileNo = (unsigned char)ReadMEM(WINaddr + (by/8) * 32 + (i)/8);
		}
		if (tileNo != oldtileNo) {
			 B1 = (unsigned char)ReadMEM(TILEaddr + ((tileNo) * 16)  + (by%8)*2 );
			 B2 = (unsigned char)ReadMEM(TILEaddr + ((tileNo) * 16)  + (by%8)*2 + 1 );
			 oldtileNo = tileNo;
		}
		if (((B1 >> (7-(i%8))) & 0x01) == 1) {
			if (((B2 >> (7-(i%8))) &0x01) == 1) { colour = 3;
			} else { colour = 2;
			}
		} else if (((B2 >> (7-(i%8))) &0x01) == 1) { colour = 1;
		} else {
			if(Transparency == 1){
				colour = screenBuffer[(LCDY * 160) + i + WNDX - 7]; // Or Better yet, skip the output.
			} else {
				colour = 0;
			}
		}
		screenBuffer[(LCDY * 160) + i + WNDX - 7] = colour;
	}
}

void DrawOBJline(int line, int TILEaddr) {

	BYTE by;  // Byte0  Y position on the screen
	BYTE bx;  // Byte1  X position on the screen
	BYTE tileNo = 0x00; // Byte2  Pattern number 0-255
	BYTE bflag = 0x00; 	/* Byte3  Flags:

	         Bit7  Priority
	               If this bit is set to 0, sprite is displayed
	               on top of background & window. If this bit
	               is set to 1, then sprite will be hidden behind
	               colors 1, 2, and 3 of the background & window.
	               (Sprite only prevails over color 0 of BG & win.)
	         Bit6  Y flip
	               Sprite pattern is flipped vertically if
	               this bit is set to 1.
	         Bit5  X flip
	               Sprite pattern is flipped horizontally if
	               this bit is set to 1.
	         Bit4  Palette number
	               Sprite colors are taken from OBJ1PAL if
	               this bit is set to 1 and from OBJ0PAL
	               otherwise.
*/
	int iflipx = 0;
	int iflipy = 0;
	int ipal   = 0;
	BYTE B1, B2;
	int colour;
	int pos;
	int i, j;
	for ( i = 0; i < 40; i++) {
		pos = i * 4;
		by = OAMRAM[pos] & 0xFF;
		bx = OAMRAM[pos + 1] & 0xFF;
		tileNo = OAMRAM[pos + 2] & 0xFF;
		bflag = OAMRAM[pos + 3] & 0xFF;
		if (( bx != 0x00 && by != 0x00) && (by <=line + 16) && (by > line + (16 - 8))) {  // 8/16
			B1 = (unsigned char)ReadMEM(TILEaddr + ((tileNo) * 16)  );
		    B2 = (unsigned char)ReadMEM(TILEaddr + ((tileNo) * 16) + 1 );

			iflipx = (bflag & 0x20) == 0x20;
			iflipy = (bflag & 0x40) == 0x40;
			ipal   = (bflag & 0x10) == 0x10;

			//Hidden (Priority Bit 7)

			for (j = 0; i < 8; i++) {

				if (((B1 >> (7-i)) & 0x01) == 0x01) {
					if (((B2 >> (7-i)) & 0x01) == 0x01) {
						if (ipal) { // Use OBJPAL1
								colour = (OBJPAL1 >>6) & 0x3; //3;
						} else {
								colour = (OBJPAL0 >>6) & 0x3;
						}
					} else {
						if (ipal) { // Use OBJPAL1
								colour = (OBJPAL1 >>4) & 0x3; //3;
						} else {
								colour = (OBJPAL0 >>4) & 0x3;
						}
					}
				} else if (((B2 >> (i)) & 0x01) == 0x01) {
						if (ipal) { // Use OBJPAL1
								colour = (OBJPAL1 >>2) & 0x3; //3;
						} else {
								colour = (OBJPAL0 >>2) & 0x3;
						}
					} else {
						if (ipal) { // Use OBJPAL1
								colour = OBJPAL1  & 0x3; //3;
						} else {
								colour = OBJPAL0  & 0x3;
						}
				}
				if ((bx - 7 + i < 160) && (bx -7 + i >= 0)) {
					screenBuffer[(line * 160) + bx - 7 + i] = colour;
				}
			}
		}
	}
}



void vblank(void){

//	curframe--;
//	if(curframe < 0) {
//		curframe = frameskip;
		if ((LCDCONTROL >> 7) == 0x01) { // LCD ON



				if((LCDCONTROL & 0x01) == 0x01) {
					PrepScreen();
					DrawBG();
					RenderWorld(0,0,0);
				}
				//if(((LCDCONTROL >> 5) & 0x01) == 0x01) {
				//	DrawWindow();
				//}

				//sprintf(buff, "PC: %04X inst:%02X A:%02X", reg_PC, ROM[reg_PC], reg_A); //writeFont(buff, 0, -170, 60);

		}
//	}
}
void runEmu(){

	loadRom();
	reset_Z80();

    temp = 0;
    Voff = 2048;
	runto = 0;

	#if defined(DEBUG)
		printf("Emulation START...\n");
	#endif

	while (EMULATING) {
		if ( IME && (IFLAG & IER) != 0) {
			interrupt();
		}
		//if (reg_PC == 0x3E6) { runto = 1; }
		pad = PadRead(0);
		if ((pad & Pad1R1) && (pad & Pad1R2) && (pad  & Pad1L1) && (pad  & Pad1L2) && (pad  & Pad1Select) && (pad  & Pad1Start)){
		//	// User wants to QUIT Emulation
			EMULATING=0;
		}
		#if defined(DEBUG)
		if (pad & Pad1R2){
			printf("PC:%04X | OP:%02X | HL:%04X | F:%02X | A:%02X | B:%02X | C:%02X | D:%02X | E:%02X | SP:%04X\n", reg_PC, (BYTE)ROM[reg_PC], reg_HL, reg_F, reg_A, reg_B, reg_C, reg_D, reg_E, reg_SP);
		}
		#endif

		//if (reg_PC > 0x0273) {
		//	printf("PC:%04X OP:%02X HL:%04X A:%02X B:%02X \n", reg_PC, (BYTE)ROM[reg_PC], reg_HL, reg_A, reg_B);
		//}
		//printf("PC:%04X OP:%02X A:%02X B:%02X C:%02X D:%02X E:%02X F:%02X\n",reg_PC, (BYTE)ROM[reg_PC], reg_A, reg_B, reg_C, reg_D, reg_E, reg_F);
		//printf("HL:%04X SP:%04X Z:%d N:%d H:%d C:%d\n", reg_HL, reg_SP, getZ(), getN(), getH(), getC());

		instructions[ReadMEM(reg_PC++)]();

		//if (cyclesLeft <= 0){
		//	doCycles();
		//}

		//temp ++;
		/*if ((runto == 1) && (temp > 1000)) {
			PrepScreen();
			temp = 0;
			DrawBG();
			//ROM[reg_PC] = 0xC9;
			sprintf(buff, "X%02X Y%02X LCDY%02X", SCRX, SCRY, LCDY);
			writeFont(buff, 0, -170, 0);
			sprintf(buff, "%04X %02X%02X Z:%dN:%dH:%dC:%d", (ReadMEM(reg_SP) << 8 | ReadMEM(reg_SP +1)), ReadMEM(reg_SP +2), ReadMEM(reg_SP +3), getZ(), getN(), getH(), getC()); //*romHeader.type);
			writeFont(buff, 0, -170, 20);
			sprintf(buff, "IER: %04X IF: %02X IME %d", IER, IFLAG, IME);
			writeFont(buff, 0, -170, 40);
			sprintf(buff, "PC: %04X inst:%02X (LBO %02X)", reg_PC, ROM[reg_PC], illegalOpcodes);
			writeFont(buff, 0, -170, 60);
        	sprintf(buff, "A: %02X B: %02X C: %02X D: %02X ", reg_A, reg_B, reg_C, reg_D);
			writeFont(buff, 0, -170, 80);
			sprintf(buff, "E: %02X HL: %04X SP: %04X", reg_E, reg_HL, reg_SP);
			writeFont(buff, 0, -170, 100);

			pad = PadRead(0);
    	    RenderWorld(0,0,0);
    	    while (!(pad & Pad1x)){
    	  		pad = PadRead(0);
    	   		if ((pad & Pad1L2) && (pad & Pad1R2)) {
			   		while (!((pad & Pad1x) && (pauseMnuPos == 0))) {
						if (pad & Pad1Up) if (pauseMnuPos != 0) pauseMnuPos--;
						if (pad & Pad1Down) if (pauseMnuPos != 5) pauseMnuPos++;
						if (pad & Pad1x) {
							if (pauseMnuPos == 1) { /// Save
							}
							if (pauseMnuPos == 2) { /// Load
							}
							if (pauseMnuPos == 3) { /// Reset
							}
							if (pauseMnuPos == 4) { /// Options
							}
							if (pauseMnuPos == 5) { /// Exit
							//free memory..
							return;
							}
						}

						PrepScreen();
						for (i = 0; i < 6; i++) {
							writeFont(pauseData[i], 0, -70, 20 * i -30);
							if (i == pauseMnuPos) writeFont("[-         -]", 0, -100, 20 * pauseMnuPos - 30);
						}
						RenderWorld(0x14,0x21, 0x76);
						pad = PadRead(0);
			  		}
		   		}
		    }
		}*/
	}
	//TODO:
	//Unload Rom
	UnAllocate_Memory();
}

void loadRom(void){
	int i;


	for (i = 0; i < 16; i++){
		CARTTITLE[i] = ROM[0x134+i];
	}
	CARTTYPE = ROM[0x147];
	ROMSIZE  = ROM[0x148];
	RAMSIZE  = ROM[0x149];
	MANUCODE[0]=ROM[0x14A];
	MANUCODE[1]=ROM[0x14B];
	VERSIONNUMBER=ROM[0x14C];

		if(RAMSIZE == 0x00) {  iRAMSIZE = 0; }
		if(RAMSIZE == 0x01) {  iRAMSIZE = 2; }
		if(RAMSIZE == 0x02) {  iRAMSIZE = 8; }
		if(RAMSIZE == 0x03) {  iRAMSIZE = 32; }

		if(ROMSIZE == 0x00) { iROMSIZE = 2; }
		if(ROMSIZE == 0x01) { iROMSIZE = 4; }
		if(ROMSIZE == 0x02) { iROMSIZE = 8; }
		if(ROMSIZE == 0x03) { iROMSIZE = 16; }
		if(ROMSIZE == 0x04) { iROMSIZE = 32; }
		if(ROMSIZE == 0x05) { iROMSIZE = 64; }
		if(ROMSIZE == 0x06) { iROMSIZE = 128; }

	#if defined(DEBUG)
		printf("Name:      %s\n", CARTTITLE);

		printf("Cart Type: %X [", CARTTYPE);
		if(CARTTYPE == 0x00) printf("ROM");
		if(CARTTYPE == 0x01) printf("MBC1");
		if(CARTTYPE == 0x02) printf("MBC1+RAM");
		if(CARTTYPE == 0x03) printf("MBC1+RAM+BATTERY");
		if(CARTTYPE == 0x05) printf("MBC2");
		if(CARTTYPE == 0x06) printf("MBC2+BATTERY");
		if(CARTTYPE == 0x08) printf("ROM+RAM");
		if(CARTTYPE == 0x09) printf("ROM+RAM+BATTERY");
		if(CARTTYPE == 0x0B) printf("MMM01");
		if(CARTTYPE == 0x0C) printf("MMM01+RAM");
		if(CARTTYPE == 0x0D) printf("MMM01+RAM+BATTERY");
		if(CARTTYPE == 0x0F) printf("MBC3+TIMER+BATTERY");
		if(CARTTYPE == 0x10) printf("MBC3+TIMER+RAM+BATTERY");
		if(CARTTYPE == 0x11) printf("MBC3");
		if(CARTTYPE == 0x12) printf("MBC3+RAM");
		if(CARTTYPE == 0x13) printf("MBC3+RAM+BATTERY");
		if(CARTTYPE == 0x15) printf("MBC4");
		if(CARTTYPE == 0x16) printf("MBC4+RAM");
		if(CARTTYPE == 0x17) printf("MBC4+RAM+BATTERY");
		if(CARTTYPE == 0x19) printf("MBC5");
		if(CARTTYPE == 0x1A) printf("MBC5+RAM");
		if(CARTTYPE == 0x1B) printf("MBC5+RAM+BATTERY");
		if(CARTTYPE == 0x1C) printf("MBC5+RUMBLE");
		if(CARTTYPE == 0x1D) printf("MBC5+RUMBLE+RAM");
		if(CARTTYPE == 0x1E) printf("MBC5+RUMBLE+RAM+BATTERY");
		if(CARTTYPE == 0xFC) printf("POCKET CAMERA");
		if(CARTTYPE == 0xFD) printf("Bandai TAMA5");
		if(CARTTYPE == 0xFE) printf("HuC3");
		if(CARTTYPE == 0xFF) printf("HuC1+RAM+BATTERY");
		printf("]\n");


		printf("Rom Size: %X [", ROMSIZE);
		if(ROMSIZE == 0x00) printf("32k");
		if(ROMSIZE == 0x01) printf("64k");
		if(ROMSIZE == 0x02) printf("128k");
		if(ROMSIZE == 0x03) printf("256k");
		if(ROMSIZE == 0x04) printf("512k");
		if(ROMSIZE == 0x05) printf("1024k");
		if(ROMSIZE == 0x06) printf("2048k");
		if(ROMSIZE == 0x07) printf("4096k");
		printf( "]\n");

		printf("Ram Size: %X\n", RAMSIZE);
		if(RAMSIZE == 0x00) { printf("0k"); }
		if(RAMSIZE == 0x01) { printf("2k"); }
		if(RAMSIZE == 0x02) { printf("8k"); }
		if(RAMSIZE == 0x03) { printf("32k");  }
		printf( "\n");

		printf("Manu. Code: %X %X\n", MANUCODE[0], MANUCODE[1]);
		printf("Version: %X\n", VERSIONNUMBER);
	#endif


}

BYTE ReadMEM(WORD loc) {
    	if (loc < 0x4000) {  // ROM Bank 0
			return ROM[loc];
		}
		if (loc < 0x8000) { // $4000-$7FFF - ROM Bank n
        	return ROM[loc + ((ROMBANKNUMBER - 1 ) * 0x4000)];
		}
		if ( loc < 0xA000 ) { // $8000-$9FFF - VRAM
			return VRAM[loc - 0x8000];
		}
		if ( loc < 0xC000 ) { // $A000-$BFFF - External (cartridge) RAM
			if (MBCMODE) { // 4/32 mode
				return EXTRNRAM[loc - 0xA000 + (WORD)(RAMBANKNUMBER * 0x2000)];
			} else {
				return EXTRNRAM[loc - 0xA000 ];
			}
		}
		if ( loc < 0xE000 ) { // $C000-$DFFF - Internal RAM
			return RAM[loc - 0xC000];
		}
		if ( loc < 0xFE00  ) { // $E000-$FDFF - Reserved Area/Echo RAM
	        return RAM[loc - 0xE000];
		}
		if ( loc < 0xFEA0  ) { // $FE00-$FE9F - Object Attribute Memory (OAM)
			return OAMRAM[loc - 0xFE00];

		}
		if ( ( loc >= 0xFF00 ) &&  ( loc <= 0xFF7F ) ) { // $FF00-$FF7F - Hardware I/O Registers

			switch (loc) {
				case 0xFF00: return (BYTE)P1; break; // P1 (R/W)
				case 0xFF01: break; // Serial transfer data (R/W)
				case 0xFF02: break; // SIO control  (R/W)
				case 0xFF04: break; // Divider Register (R/W)
				case 0xFF05: return (BYTE)TIMECNT; break;// Timer counter (R/W)
				case 0xFF06: return (BYTE)TIMEMOD; break;// Timer Modulo (R/W)
				case 0xFF07: return (BYTE)TIMCONT; break; // Timer Control
				case 0xFF0F: return (BYTE)IFLAG; break; // Interrupt Flag (R/W)

				// SOUND
				case 0xFF10: break; // Sound Mode 1 register, Sweep register (R/W)
				case 0xFF11: break; // Sound Mode 1 register, Sound length/Wave pattern duty (R/W)
				case 0xFF12: break; // Sound Mode 1 register, Envelope (R/W)
				case 0xFF13: break; // Sound Mode 1 register, Frequency lo (W)
				case 0xFF14: break; // Sound Mode 1 register, Frequency hi (R/W)

				case 0xFF16: break; // Sound Mode 2 register, Sound Length; Wave Pattern Duty (R/W)
				case 0xFF17: break; // Sound Mode 2 register, envelope (R/W)
				case 0xFF18: break; // Sound Mode 2 register, frequency lo data (W)
				case 0xFF19: break; // Sound Mode 2 register, frequency hi data (R/W)
				case 0xFF1A: break; // Sound Mode 3 register, Sound on/off (R/W)
				case 0xFF1B: break; // Sound Mode 3 register, sound length (R/W)
				case 0xFF1C: break; // Sound Mode 3 register, Select output level
				case 0xFF1D: break; // Sound Mode 3 register, frequency's lower data (W)
				case 0xFF1E: break; // Sound Mode 3 register, frequency's higher data (R/W)
				case 0xFF20: break; // Sound Mode 4 register, sound length (R/W)
				case 0xFF21: break; // Sound Mode 4 register, envelope (R/W)
				case 0xFF22: break; // Sound Mode 4 register, polynomial counter (R/W)
				case 0xFF30: break; // Sound Mode 4 register, counter/consecutive; inital (R/W)

				case 0xFF24: break; // Channel control / ON-OFF / Volume (R/W)
				case 0xFF25: break; // Selection of Sound output terminal (R/W)
				case 0xFF26: break; // Sound on/off (R/W)



			// VIDEO
				case 0xFF40: return (BYTE)LCDCONTROL; break; // LCD Control (R/W)
				case 0xFF41: return (BYTE)LCDSTATUS; break; // LCDC Status   (R/W)
				case 0xFF42: return (BYTE)SCRY; break; // Scroll Y   (R/W)
				case 0xFF43: return (BYTE)SCRX; break; // Scroll X   (R/W)
				case 0xFF44: return (BYTE)LCDY; break; // LCDC Y-Coordinate (R)
				case 0xFF45: return (BYTE)LYC; break; // LY Compare  (R/W)
				case 0xFF47: return (BYTE)BGPAL; break;// BG Palette Data  (W)
				case 0xFF48: return (BYTE)OBJPAL0; break; // Object Palette 0 Data (W)
				case 0xFF49: return (BYTE)OBJPAL1; break; // Object Palette 1 Data (W)
				case 0xFF4A: return (BYTE)WNDY; break; // Window Y Position  (R/W)
				case 0xFF4B: return (BYTE)WNDX; break; // Window X Position  (R/W)

				default: return 0x00; break;
			}

		} else if ( ( loc >= 0xFF80 ) &&  ( loc <= 0xFFFE ) ) { // $FF80-$FFFE - High RAM Area
			return HIRAM[loc - 0xFF80];
		} else if  ( loc == 0xFFFF ) { // $FFFF - Interrupt Enable Register
	        return (BYTE)IER;
		}
         else { return 0x00; }

}

void WriteMEM(WORD loc, BYTE b){
   	if	  ( ( loc <= 0x3FFF)) {

		if ( loc >= 0x2000) {

			// MBC1
			if (( CARTTYPE == 0x01 ) || ( CARTTYPE == 0x02) || ( CARTTYPE == 0x03 )) {
				//This is MBC1
				if (!b) b = 1;
				ROMBANKNUMBER = (b & 0x1F);
				#if defined(DEBUG)
				printf("Switching MBC1 to %d. [PC: %04X | LOC: %04X]\n", ROMBANKNUMBER, reg_PC, loc);
				#endif
			}
			// MBC2
			if (( CARTTYPE == 0x05 ) || ( CARTTYPE == 0x06 )) {
				if (!b) b = 1;
				ROMBANKNUMBER = (b & 0x0F);
				#if defined(DEBUG)
				printf("Switching MBC2 to %d. [PC: %04X | LOC: %04X]\n", ROMBANKNUMBER, reg_PC, loc);
				#endif
			}

		}
	} else if ( ( loc >= 0x4000 ) && ( loc <= 0x7FFF ) ) { // $4000-$7FFF - ROM Bank n
		if ( loc <= 0x5FFF ) { //TODO: Add Test for MBC1
			if (( CARTTYPE == 0x01 ) || ( CARTTYPE == 0x02) || ( CARTTYPE == 0x03 )) {
				if (MBCMODE) {
					RAMBANKNUMBER = (b & 0x03);
				} else {
					#if defined(DEBUG)
						printf("I dont know.. Set the two most significant ROM Adress Lines!");
					#endif
				}
			}
			#if defined(DEBUG)
				printf("Switching RAM MBC1 to %d. [PC: %04X | LOC: %04X]\n", b, reg_PC, loc);
			#endif
			RAMBANKNUMBER = b;
		} else {
			if (b & 0x01) {
				#if defined(DEBUG)
					printf("4/32 Memory mode selected\n");
				#endif
				MBCMODE = 1;
			} else {
				#if defined(DEBUG)
					printf("16/8 Memory mode selected\n");
				#endif
				MBCMODE = 0;
			}
		}

	} else if ( ( loc >= 0x8000 ) &&  ( loc <= 0x9FFF ) ) { // $8000-$9FFF VRAM
		VRAM[loc - 0x8000] = b;
	} else if ( ( loc >= 0xA000 ) &&  ( loc <= 0xBFFF ) ) { // $A000-$BFFF - External (cartridge) RAM
		EXTRNRAM[loc - 0xA000] = b;
	} else if ( ( loc >= 0xC000 ) &&  ( loc <= 0xDFFF ) ) { // $C000-$DFFF - Internal RAM
		RAM[loc - 0xC000] = b;
	} else if ( ( loc >= 0xE000 ) &&  ( loc <= 0xFDFF ) ) { // $E000-$FDFF - Reserved Area/Echo RAM
		RAM[loc - 0xE000] = b;
	} else if ( ( loc >= 0xFE00 ) &&  ( loc <= 0xFE9F ) ) { // $FE00-$FE9F - Object Attribute Memory (OAM)
		OAMRAM[loc - 0xFE00] = b;
	} else if ( ( loc >= 0xFF00 ) &&  ( loc <= 0xFF7F ) ) { // $FF00-$FF7F - Hardware I/O Registers
		switch (loc) {
			case 0xFF00: if (b == 0x03) { P1 = 0xF1; // Register for reading joy pad info and determining system type.    (R/W)
					} else {
						 pad = PadRead(0);

						 if(b  == 0x10) {
							 P1 = 0x00;
							 if (pad & Pad1x) { P1 |= 0x01; }
							 if (pad & Pad1crc) { P1 |= 0x02; }
							 if (pad & Pad1Select) { P1 |= 0x04; }
							 if (pad & Pad1Start) { P1 |= 0x08; }
							 if (pad & Pad1R1) { P1 |= 0x03; } // Press both a + b
							 P1 = ~P1;
							 P1 &= 0x0F;
						 	 break;
						 }
						 if(b == 0x20) {
							 P1 = 0x00;
							 if (pad & Pad1Right) { P1 |= 0x01; }
							 if (pad & Pad1Left) { P1 |= 0x02; }
							 if (pad & Pad1Up) { P1 |= 0x04; }
							 if (pad & Pad1Down) { P1 |= 0x08; }
							 P1 = ~P1;
							 P1 &= 0x0F;
							 break;
						 }
					}
					break;
			case 0xFF01: break; // Serial transfer data (R/W)
			case 0xFF02: break; // SIO control  (R/W)
			case 0xFF04: break; // Divider Register (R/W)
			case 0xFF05: TIMECNT = b; break; // Timer counter (R/W)
			case 0xFF06: TIMEMOD = b; break; // Timer Modulo (R/W)
			case 0xFF07: TIMCONT = b;
							//set Timer rate
							if ((TIMCONT & 0x3) == 0) {
								MAXTIME = 1024;
							} else if ((TIMCONT & 0x3) == 1) {
								MAXTIME = 16;
							} else if ((TIMCONT & 0x3) == 2) {
								MAXTIME = 64;
							} else if ((TIMCONT & 0x3) == 3) {
								MAXTIME = 256;
							}
							break; // Timer Control
			case 0xFF0F: IFLAG = b; break; // Interrupt Flag (R/W)

			// SOUND
			case 0xFF10: break; // Sound Mode 1 register, Sweep register (R/W)
			case 0xFF11: break; // Sound Mode 1 register, Sound length/Wave pattern duty (R/W)
			case 0xFF12: break; // Sound Mode 1 register, Envelope (R/W)
			case 0xFF13: break; // Sound Mode 1 register, Frequency lo (W)
			case 0xFF14: break; // Sound Mode 1 register, Frequency hi (R/W)

			case 0xFF16: break; // Sound Mode 2 register, Sound Length; Wave Pattern Duty (R/W)
			case 0xFF17: break; // Sound Mode 2 register, envelope (R/W)
			case 0xFF18: break; // Sound Mode 2 register, frequency lo data (W)
			case 0xFF19: break; // Sound Mode 2 register, frequency hi data (R/W)
			case 0xFF1A: break; // Sound Mode 3 register, Sound on/off (R/W)
			case 0xFF1B: break; // Sound Mode 3 register, sound length (R/W)
			case 0xFF1C: break; // Sound Mode 3 register, Select output level
			case 0xFF1D: break; // Sound Mode 3 register, frequency's lower data (W)
			case 0xFF1E: break; // Sound Mode 3 register, frequency's higher data (R/W)
			case 0xFF20: break; // Sound Mode 4 register, sound length (R/W)
			case 0xFF21: break; // Sound Mode 4 register, envelope (R/W)
			case 0xFF22: break; // Sound Mode 4 register, polynomial counter (R/W)
			case 0xFF30: break; // Sound Mode 4 register, counter/consecutive; inital (R/W)

			case 0xFF24: break; // Channel control / ON-OFF / Volume (R/W)
			case 0xFF25: break; // Selection of Sound output terminal (R/W)
			case 0xFF26: break; // Sound on/off (R/W)

			// VIDEO
			case 0xFF40: LCDCONTROL = b; break; // LCD Control (R/W)
			case 0xFF41: LCDSTATUS = b; break; // LCDC Status   (R/W)
			case 0xFF42: SCRY = b; break; // Scroll Y   (R/W)
			case 0xFF43: SCRX = b; break; // Scroll X   (R/W)
			case 0xFF44: LCDY = 0x00; break; // LCDC Y-Coordinate (R)
			case 0xFF45: LYC = b; break; // LY Compare  (R/W)
			case 0xFF46: doDMA(b); break; // DMA Transfer and Start Address (W)
			case 0xFF47: BGPAL = b; break; // BG Palette Data  (W)
			case 0xFF48: OBJPAL0 = b; break; // Object Palette 0 Data (W)
			case 0xFF49: OBJPAL1 = b; break; // Object Palette 1 Data (W)
			case 0xFF4A: WNDY = b; break; // Window Y Position  (R/W)
			case 0xFF4B: WNDX = b; break; // Window X Position  (R/W)
			default: break;
		}

	} else if ( ( loc >= 0xFF80 ) &&  ( loc <= 0xFFFE ) ) { // $FF80-$FFFE - High RAM Area
		HIRAM[loc - 0xFF80] = b;

	} else if  ( loc == 0xFFFF ) { // $FFFF - Interrupt Enable Register
          IER = (int)b;
	}
}

WORD ReadWord(WORD reg){
	WORD aW = ReadMEM(reg) | (ReadMEM(reg + 1) << 8);
	return aW & 0xFFFF;
}

void DrawBG(){
	//////////////here
	//printf("DrawBG\n");
	Draw_Buffer(screenBuffer);
}
void DrawWindow(){

}

void ShowTiles(int tileNo) {
	//setWH(&pix, 2, 2);
	BYTE B1, B2;
	for (t = 0; t < 16; t+=2) {
		B1 = ROM[(WORD)((tileNo * 16) + t + Voff)];
		B2 = ROM[(WORD)((tileNo * 16) + t + 1 + Voff)];

		for (n = 0; n < 8; n++) {
			//SetTile(&pix);
			//pix.x0 = (((0) * 8) + n) * pix.w;
			//pix.y0 = ((int)(t/2)) * pix.h ;
			//pix.r0 = pix.g0 = pix.b0 = (u_char)((int)((B1 >> n) & (0x00000001)) + (int)((B2 >> n) & (0x00000001))) * 100;
			//DrawPrim(&pix);
		}
	}
}

void DrawTile(int tileNo, int Xpos, int Ypos){

	//setWH(&pix, 2, 2);
	BYTE B1, B2;
	for (t = 0; t < 16; t+=2) {
		B1 = ROM[(WORD)((tileNo * 16 * 2) + t + 0x1800)];
		B2 = ROM[(WORD)((tileNo * 16 * 2) + t + 1 + 0x1800)];

		for (n = 0; n < 8; n++) {
			//SetTile(&pix);
			//pix.x0 = (Xpos + n) * pix.w;
			//pix.y0 = (Ypos + t/2) * pix.h ;
			////pix.r0 = pix.g0 = pix.b0 = (u_char)((int)((B1 >> n) & (0x00000001)) + (int)((B2 >> n) & (0x00000001))) * 100;
			//DrawPrim(&pix);
		}
	}
}


/*
Not Mapped yet:
--------------

27    DAA
76    HALT

*/

void OP00(void){  // 00	NOP
	cycleLength(4);
}

void OP01(void){  // 01	LD	BC,nnnn
	put_rBC(ReadWord(reg_PC));
	reg_PC += 2;
	cycleLength(20);
}

void OP02(void){
	WriteMEM(get_rBC(), reg_A);
	cycleLength(7);
} // 02	LD	(BC),A

void OP03(void){ //  case 0x03:
	put_rBC(INCWreg(get_rBC()));
	cycleLength(6);
} // 03	INC	BC

void OP04(void){ // case 0x04:
	reg_B = INCreg(reg_B);
	cycleLength(4);
} // 04	INC	B

void OP05(void){ //		case 0x05:
	reg_B = DECreg(reg_B);
	cycleLength(4);
} // 05	DEC B

void OP06(void){ //		case 0x06:
	reg_B = ReadMEM(reg_PC++);
	cycleLength(8);
} // 06	LD	B,nn

void OP07(void){ // case 0x07:
	reg_A = RLC(reg_A);
	cycleLength(4);
} // 07    RLCA

void OP08(void){ //		case 0x08:
	WORD tmp;
	tmp = ReadWord(reg_PC);
	WriteMEM(tmp, reg_SP & 0xFF);
	WriteMEM(tmp+1, reg_SP >> 8);
	reg_PC += 2;

	//BYTE tmp1, tmp2;
	//tmp1 = ReadMEM(reg_PC++);
    //tmp2 = ReadMEM(reg_PC++);
    //WriteMEM(((tmp2 << 8)| tmp1), reg_SP & 0xFF); // 08    LD   (nnnn),SP
    //WriteMEM((((tmp2 << 8) | tmp1)+1), reg_SP >> 8);
	cycleLength(20);
}

void OP09(void){ //		case 0x09:
	reg_HL = ADDWreg(reg_HL, get_rBC());
	cycleLength(12);
} // 09    ADD  HL,BC

void OP0A(void){ //		case 0x0A:
	reg_A = ReadMEM(get_rBC());
cycleLength(8); } // 0A    LD   A,(BC)

void OP0B(void){ //		case 0x0B:
	put_rBC(DECWreg(get_rBC()));
cycleLength(8); } // 0B    DEC  BC

void OP0C(void){ //		case 0x0C:
	reg_C = INCreg(reg_C);
	cycleLength(4);
} // 0C    INC  C

void OP0D(void){ //		case 0x0D:
	reg_C = DECreg(reg_C);
	cycleLength(4);
} // 0D    DEC  C

void OP0E(void){ //		case 0x0E:
	reg_C = ReadMEM(reg_PC++);
	cycleLength(8);
}  // 0E    LD   C,nn

void OP0F(void){ //		case 0x0F:
	reg_A = RRC(reg_A);
cycleLength(4); } // 0F    RRCA

void OP10(void){ //  0x10:
	ReadMEM(reg_PC++);
cycleLength(4); } // 10 00 STOP      ???

void OP11(void){ //  0x11:
	put_rDE(ReadWord(reg_PC));
	reg_PC += 2;
	cycleLength(12);
} // 11    LD   DE,nnnn

void OP12(void){ //  0x12:
	WriteMEM(get_rDE(), reg_A);
	cycleLength(8);
} // 12    LD   (DE),A

void OP13(void){ //  0x13:
	put_rDE(INCWreg(get_rDE()));
	cycleLength(8);
} // 13    INC  DE

void OP14(void){ // case  0x14:
	reg_D = INCreg(reg_D);
 cycleLength(4); } // 14    INC  D

void OP15(void){ // case  0x15:
reg_D = DECreg(reg_D);
cycleLength(4); } // 15    DEC  D

void OP16(void){ // case  0x16:
		reg_D = ReadMEM(reg_PC++); cycleLength(8); } // 16    LD   D,nn

void OP17(void){ // case  0x17:
reg_A = RLA(reg_A);cycleLength(4); } // 17    RLA

void OP18(void){ // case  0x18:
	reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
	cycleLength(12);
} // 18    JR   disp

void OP19(void){ // case  0x19:
		reg_HL = ADDWreg(reg_HL, get_rDE());
		cycleLength(16);
} //19    ADD  HL,DE

void OP1A(void){ // case  0x1A:
	reg_A = ReadMEM(get_rDE());
	cycleLength(8);
} // 1A    LD   A,(DE)

void OP1B(void){ // case  0x1B:
	put_rDE(DECWreg(get_rDE()));
	cycleLength(4);
} // 1B    DEC  DE

void OP1C(void){
	reg_E = INCreg(reg_E);
	cycleLength(4);
} // 1C    INC  E

void OP1D(void){ // case  0x1D:
	reg_E = DECreg(reg_E);
	cycleLength(4);
} // 1D    DEC  E

void OP1E(void){ // case  0x1E:
	reg_E = ReadMEM(reg_PC++);
	cycleLength(8);
} // 1E    LD   E,nn

void OP1F(void){ // case  0x1F:
	reg_A = RRA(reg_A);
	cycleLength(4);
}  // 1F    RRA

void OP20(void){ // case  0x20:
	if (!getZ()) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else {
		ReadMEM(reg_PC++);
		cycleLength(8);
	} // 20    JR   NZ,disp
}

void OP21(void){ // case  0x21:
	reg_HL = ReadWord(reg_PC);
	reg_PC += 2;
	cycleLength(12);
} // 21    LD   HL,nnnn
void OP22(void){ // case  0x22:
	WriteMEM(reg_HL, reg_A);
	reg_HL = INCWreg(reg_HL);
	cycleLength(16);
} // 22    LDI  (HL),A

void OP23(void){ // case  0x23:
	reg_HL = INCWreg(reg_HL);
	cycleLength(8);
} // 23    INC  HL

void OP24(void){ // case  0x24:
	put_rH(INCreg(get_rH()));
	cycleLength(4);
} // 24    INC  H

void OP25(void){ // case  0x25:
	put_rH(DECreg(get_rH()));
	cycleLength(4);
} // 25    DEC  H

void OP26(void){ // case  0x26:
	put_rH(ReadMEM(reg_PC++));
	cycleLength(8);
} // 26    LD   H,nn

void OP27(void){
	printf("Incomplete Opcode 27\n");
	cycleLength(4);
}// case  27? cycleLength(4);

void OP28(void){ // case  0x28:
	if (getZ()) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else {
		ReadMEM(reg_PC++);
		cycleLength(8);
	}
}// 28    JR   Z,disp

void OP29(void){ // case  0x29:
	reg_HL = ADDWreg(reg_HL, reg_HL);
	cycleLength(12);
} // 29    ADD  HL,HL

void OP2A(void){ // case  0x2A:
	reg_A = ReadMEM(reg_HL);
	reg_HL = INCWreg(reg_HL);
	cycleLength(16);
} // 2A    LDI  A,(HL)

void OP2B(void){ // case  0x2B:
	reg_HL = DECWreg(reg_HL);
	cycleLength(8);
} // 2B    DEC  HL

void OP2C(void){ // case  0x2C:
	put_rL(INCreg(get_rL()));
	cycleLength(4);
} // 2C    INC  L

void OP2D(void){ // case  0x2D:
	put_rL(DECreg(get_rL()));
	cycleLength(4);
} // 2D    DEC  L

void OP2E(void){ // case  0x2E:
	put_rL(ReadMEM(reg_PC++));
	cycleLength(8);
} // 2E    LD   L,nn

void OP2F(void){ // case  0x2F:
	reg_A = CPLreg(reg_A);
	cycleLength(4);
} // 2F    CPL

void OP30(void){ // case  0x30:
	if (getC() != 1) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else { ReadMEM(reg_PC++);
		cycleLength(8);
	}
} // 30    JR   NC,disp

void OP31(void){ // case  0x31:
	reg_SP = ReadWord(reg_PC);
	reg_PC += 2;
	cycleLength(12);
} //31    LD   SP,nnnn

void OP32(void){ // case  0x32:
	WriteMEM(reg_HL, reg_A);
	reg_HL = DECWreg(reg_HL);
	cycleLength(16);
} // 32    LDD  (HL),A

void OP33(void){ // case  0x33:
reg_SP = INCWreg(reg_SP); cycleLength(8); }  // 33    INC  SP

void OP34(void){ // case  0x34:
	WriteMEM(reg_HL, INCreg(ReadMEM(reg_HL)));
	cycleLength(12);
} // 34    INC  (HL)

void OP35(void){ // case  0x35:
	WriteMEM(reg_HL, DECreg(ReadMEM(reg_HL)));
	cycleLength(12);
} // 35    DEC  (HL)

void OP36(void){ // case  0x36:
	WriteMEM(reg_HL, ReadMEM(reg_PC++));
	cycleLength(12);
} // 36    LD   (HL),nn

void OP37(void){ // case  0x37:
	setC(1); //TODO TEST!
	cycleLength(4);
} // 37    SCF

void OP38(void){ // case  0x38:
	if (getC() != 0) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else {
		ReadMEM(reg_PC++);
		cycleLength(8);
	}
} // 38    JR   C,disp

void OP39(void){ // case  0x39:
	reg_HL = ADDWreg(reg_HL, reg_SP);
	cycleLength(12);
} // 39    ADD  HL,SP

void OP3A(void){ // case  0x3A:
	reg_A = ReadMEM(reg_HL);
	reg_HL = DECWreg(reg_HL);
	cycleLength(16);
} // 3A    LDD  A,(HL)

void OP3B(void){ // case  0x3B:
reg_SP = DECWreg(reg_SP); cycleLength(8); }	// 3B    DEC  SP
void OP3C(void){ // case  0x3C:
reg_A = INCreg(reg_A); cycleLength(4); } // 3C    INC  A

void OP3D(void){ // case  0x3D:
	reg_A = DECreg(reg_A);
	cycleLength(4);
} // 3D    DEC  A

void OP3E(void){ // case  0x3E:
	reg_A = ReadMEM(reg_PC++);
	cycleLength(8);
} // 3E    LD   A,nn

void OP3F(void){ // case  0x3F:
setC(!getC()); cycleLength(4); }// 3F    CCF
void OP40(void){ // case  0x40:
cycleLength(4); } // 40    LD   B,B
void OP41(void){ // case  0x41:
reg_B = reg_C; cycleLength(4); } // 41    LD   B,C
void OP42(void){ // case  0x42:
reg_B = reg_D; cycleLength(4); } // 42    LD   B,D
void OP43(void){ // case  0x43:
reg_B = reg_E; cycleLength(4); } // 43    LD   B,E
void OP44(void){ // case  0x44:
reg_B = get_rH(); cycleLength(4);} // 44    LD   B,H
void OP45(void){ // case  0x45:
reg_B = get_rL(); cycleLength(4);}// 45    LD   B,L
void OP46(void){ // case  0x46:
reg_B = ReadMEM(reg_HL); cycleLength(8); } // 46    LD   B,(HL)
void OP47(void){ // case  0x47:
reg_B = reg_A; cycleLength(4); } // 47    LD   B,A
void OP48(void){ // case  0x48:
reg_C = reg_B; cycleLength(4); } // 48    LD   C,B
void OP49(void){ // case  0x49:
cycleLength(4); } // 49    LD   C,C

void OP4A(void){ // case  0x4A:
	reg_C = reg_D;
	cycleLength(4);
} // 4A    LD   C,D

void OP4B(void){ // case  0x4B:
reg_C = reg_E; cycleLength(4); } // 4B    LD   C,E
void OP4C(void){ // case  0x4C:
reg_C = get_rH(); cycleLength(4); } // 4C    LD   C,H
void OP4D(void){ // case  0x4D:
reg_C = get_rL(); cycleLength(4); } // 4D    LD   C,L
void OP4E(void){ // case  0x4E:
reg_C = ReadMEM(reg_HL); cycleLength(8); } // 4E    LD   C,(HL)
void OP4F(void){ // case  0x4F:
reg_C = reg_A; cycleLength(4); } // 4F    LD   C,A
void OP50(void){ // case  0x50:
reg_D = reg_B; cycleLength(4); } // 50    LD   D,B
void OP51(void){ // case  0x51:
reg_D = reg_C; cycleLength(4); } // 51    LD   D,C
void OP52(void){ // case  0x52:
cycleLength(4); } // 52    LD   D,D
void OP53(void){ // case  0x53:
reg_D = reg_E; cycleLength(4); } // 53    LD   D,E
void OP54(void){ // case  0x54:
reg_D = get_rH(); cycleLength(4); } // 54    LD   D,H
void OP55(void){ // case  0x55:
reg_D = get_rL(); cycleLength(4); } // 55    LD   D,L
void OP56(void){ // case  0x56:
reg_D = ReadMEM(reg_HL); cycleLength(8); } // 56    LD   D,(HL)

void OP57(void){ // case  0x57:
	reg_D = reg_A;
	cycleLength(4);
} // 57    LD   D,A

void OP58(void){ // case  0x58:
reg_E = reg_B; cycleLength(4); } // 58    LD   E,B
void OP59(void){ // case  0x59:
reg_E = reg_C; cycleLength(4); } // 59    LD   E,C
void OP5A(void){ // case  0x5A:
reg_E = reg_D; cycleLength(4); } // 5A    LD   E,D
void OP5B(void){ // case  0x5B:
cycleLength(4); } // 5B    LD   E,E
void OP5C(void){ // case  0x5C:
reg_E = get_rH(); cycleLength(4); } // 5C    LD   E,H
void OP5D(void){ // case  0x5D:
reg_E = get_rL(); cycleLength(4); } // 5D    LD   E,L
void OP5E(void){ // case  0x5E:
reg_E = ReadMEM(reg_HL); cycleLength(8); } // 5E    LD   E,(HL)

void OP5F(void){ // case  0x5F:
	reg_E = reg_A;
	cycleLength(4);
} // 5F    LD   E,A

void OP60(void){ // case  0x60:
put_rH(reg_B); cycleLength(4); } // 60    LD   H,B
void OP61(void){ // case  0x61:
put_rH(reg_C); cycleLength(4); } // 61    LD   H,C
void OP62(void){ // case  0x62:
put_rH(reg_D); cycleLength(4); } // 62    LD   H,D
void OP63(void){ // case  0x63:
put_rH(reg_E); cycleLength(4); } // 63    LD   H,E
void OP64(void){ // case  0x64:
put_rH(get_rH()); cycleLength(4); } // 64    LD   H,H
void OP65(void){ // case  0x65:
put_rH(get_rL()); cycleLength(4); } // 65    LD   H,L

void OP66(void){ // case  0x66:
	put_rH((BYTE)ReadMEM(reg_HL));
	cycleLength(8);
} // 66    LD   H,(HL)

void OP67(void){ // case  0x67:
put_rH(reg_A); cycleLength(4); } // 67    LD   H,A
void OP68(void){ // case  0x68:
put_rL(reg_B); cycleLength(4); } // 68    LD   L,B
void OP69(void){ // case  0x69:
put_rL(reg_C); cycleLength(4); } // 69    LD   L,C
void OP6A(void){ // case  0x6A:
put_rL(reg_D); cycleLength(4); } // 6A    LD   L,D

void OP6B(void){ // case  0x6B:
	put_rL(reg_E);
	cycleLength(4);
} // 6B    LD   L,E

void OP6C(void){ // case  0x6C:
put_rL(get_rH()); cycleLength(4); } // 6C    LD   L,H
void OP6D(void){ // case  0x6D:
put_rL(get_rL()); cycleLength(4); } // 6D    LD   L,L
void OP6E(void){ // case  0x6E:
put_rL(ReadMEM(reg_HL)); cycleLength(8); } // 6E    LD   L,(HL)

void OP6F(void){ // case  0x6F:
	put_rL(reg_A);
	cycleLength(4);
} // 6F    LD   L,A

void OP70(void){ // case  0x70:
WriteMEM(reg_HL, reg_B); cycleLength(8); } // 70    LD   (HL),B
void OP71(void){ // case  0x71:
WriteMEM(reg_HL, reg_C); cycleLength(8); } // 71    LD   (HL),C
void OP72(void){ // case  0x72:
WriteMEM(reg_HL, reg_D); cycleLength(8); } // 72    LD   (HL),D
void OP73(void){ // case  0x73:
WriteMEM(reg_HL, reg_E); cycleLength(8); } // 73    LD   (HL),E
void OP74(void){ // case  0x74:
WriteMEM(reg_HL, get_rH()); cycleLength(8); } // 74    LD   (HL),H
void OP75(void){ // case  0x75:
WriteMEM(reg_HL, get_rL()); cycleLength(8); } // 75    LD   (HL),L

void OP76(void){// 76 HALT
	//printf("HALT\n");
	if(IME) {

		cycleLength(CyclesLeft());
	} else {

		cycleLength(4);
	}
}// 0x7676

void OP77(void){ // case  0x77:
WriteMEM(reg_HL, reg_A); cycleLength(8); } // 77    LD   (HL),A
void OP78(void){ // case  0x78:
reg_A = reg_B; cycleLength(4); } //	78    LD   A,B
void OP79(void){ // case  0x79:
reg_A = reg_C; cycleLength(4); } //	79    LD   A,C
void OP7A(void){ // case  0x7A:
reg_A = reg_D; cycleLength(4); } //	7A    LD   A,D
void OP7B(void){ // case  0x7B:
reg_A = reg_E; cycleLength(4); } //	7B    LD   A,E

void OP7C(void){ // case  0x7C:
	reg_A = get_rH();
	cycleLength(4);
} //	7C    LD   A,H

void OP7D(void){ // case  0x7D:
	reg_A = get_rL();
	cycleLength(4);
} //	7D    LD   A,L

void OP7E(void){ // case  0x7E:
	reg_A = ReadMEM(reg_HL);
	cycleLength(8);
} // 7E    LD   A,(HL)

void OP7F(void){ // case  0x7F:
	cycleLength(4);
} //	7F    LD   A,A

void OP80(void){ // case  0x80:
	reg_A = ADDreg(reg_A, reg_B);
	cycleLength(4);
} // 80    ADD  A,B

void OP81(void){ // case  0x81:
	reg_A = ADDreg(reg_A, reg_C);
	cycleLength(4);
} // 81    ADD  A,C

void OP82(void){ // case  0x82:
	reg_A = ADDreg(reg_A, reg_D);
	cycleLength(4);
} // 82    ADD  A,D

void OP83(void){ // case  0x83:
	reg_A = ADDreg(reg_A, reg_E);
	cycleLength(4);
} // 83    ADD  A,E

void OP84(void){ // case  0x84:
	reg_A = ADDreg(reg_A, get_rH());
	cycleLength(4);
} // 84    ADD  A,H

void OP85(void){ // case  0x85:
	reg_A = ADDreg(reg_A, get_rL());
	cycleLength(4);
} // 85    ADD  A,L

void OP86(void){ // case  0x86:
	reg_A = ADDreg(reg_A, ReadMEM(reg_HL));
	cycleLength(8);
} // 86    ADD  A,(HL)

void OP87(void){ // case  0x87:
	reg_A = ADDreg(reg_A, reg_A);
	cycleLength(4);
} // 87    ADD  A,A

void OP88(void){ // case  0x88:
	reg_A = ADCreg(reg_A, reg_B);
	cycleLength(4);
} // 88    ADC  A,B

void OP89(void){ // case  0x89:
	reg_A = ADCreg(reg_A, reg_C);
	cycleLength(4);
} // 89    ADC  A,C

void OP8A(void){ // case  0x8A:
	reg_A = ADCreg(reg_A, reg_D);
	cycleLength(4);
} // 8A    ADC  A,D

void OP8B(void){ // case  0x8B:
	reg_A = ADCreg(reg_A, reg_E);
	cycleLength(4);
} // 8B    ADC  A,E

void OP8C(void){ // case  0x8C:
	reg_A = ADCreg(reg_A, get_rH());
	cycleLength(4);
} // 8C    ADC  A,H

void OP8D(void){ // case  0x8D:
	reg_A = ADCreg(reg_A, get_rL());
	cycleLength(4);
} // 8D    ADC  A,L

void OP8E(void){ // case  0x8E:
	reg_A = ADCreg(reg_A, ReadMEM(reg_HL));
	cycleLength(8);
} // 8E    ADC  A,(HL)

void OP8F(void){ // case  0x8F:
	reg_A = ADCreg(reg_A, reg_A);
	cycleLength(4);
} // 8F    ADC  A,A

void OP90(void){ // case  0x90:
	reg_A = SUBreg(reg_A, reg_B);
	cycleLength(4);
} // 90    SUB  B

void OP91(void){ // case  0x91:
	reg_A = SUBreg(reg_A, reg_C);
	cycleLength(4);
} // 91    SUB  C

void OP92(void){ // case  0x92:
	reg_A = SUBreg(reg_A, reg_D);
	cycleLength(4);
} // 92    SUB  D

void OP93(void){ // case  0x93:
	reg_A = SUBreg(reg_A, reg_E);
	cycleLength(4);
} // 93    SUB  E

void OP94(void){ // case  0x94:
	reg_A = SUBreg(reg_A, get_rH());
	cycleLength(4);
} // 94    SUB  H

void OP95(void){ // case  0x95:
	reg_A = SUBreg(reg_A, get_rL());
	cycleLength(4);
} // 95    SUB  L

void OP96(void){ // case  0x96:
	reg_A = SUBreg(reg_A, ReadMEM(reg_HL));
	cycleLength(8);
} // 96    SUB  (HL)

void OP97(void){ // case  0x97:
reg_A = SUBreg(reg_A, reg_A); cycleLength(4); } // 97    SUB  A
void OP98(void){ // case  0x98:
reg_A = SBCreg(reg_A, reg_B); cycleLength(4); } // 98    SBC  A,B
void OP99(void){ // case  0x99:
reg_A = SBCreg(reg_A, reg_C); cycleLength(4); } // 99    SBC  A,C
void OP9A(void){ // case  0x9A:
reg_A = SBCreg(reg_A, reg_D); cycleLength(4); } // 9A    SBC  A,D
void OP9B(void){ // case  0x9B:
reg_A = SBCreg(reg_A, reg_E); cycleLength(4); } // 9B    SBC  A,E
void OP9C(void){ // case  0x9C:
reg_A = SBCreg(reg_A, get_rH()); cycleLength(4); } // 9C    SBC  A,H
void OP9D(void){ // case  0x9D:
reg_A = SBCreg(reg_A, get_rL()); cycleLength(4); } // 9D    SBC  A,L
void OP9E(void){ // case  0x9E:
reg_A = SBCreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // 9E    SBC  A,(HL)
void OP9F(void){ // case  0x9F:
reg_A = SBCreg(reg_A, reg_A); cycleLength(4); } // 9F    SBC  A,A
void OPA0(void){ // case  0xA0:
reg_A = ANDreg(reg_A, reg_B); cycleLength(4); } // A0    AND  B
void OPA1(void){ // case  0xA1:
reg_A = ANDreg(reg_A, reg_C); cycleLength(4); } // A1    AND  C
void OPA2(void){ // case  0xA2:
reg_A = ANDreg(reg_A, reg_D); cycleLength(4); } // A2    AND  D
void OPA3(void){ // case  0xA3:
reg_A = ANDreg(reg_A, reg_E); cycleLength(4); } // A3    AND  E
void OPA4(void){ // case  0xA4:
reg_A = ANDreg(reg_A, get_rH()); cycleLength(4); } // A4    AND  H
void OPA5(void){ // case  0xA5:
reg_A = ANDreg(reg_A, get_rL()); cycleLength(4); } // A5    AND  L
void OPA6(void){ // case  0xA6:
reg_A = ANDreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // A6    AND  (HL)
void OPA7(void){ // case  0xA7:
reg_A = ANDreg(reg_A, reg_A); cycleLength(4); } // A7    AND  A
void OPA8(void){ // case  0xA8:
reg_A = XORreg(reg_A, reg_B); cycleLength(4); } // A8    XOR  B
void OPA9(void){ // case  0xA9:
reg_A = XORreg(reg_A, reg_C); cycleLength(4); } // A9    XOR  C
void OPAA(void){ // case  0xAA:
reg_A = XORreg(reg_A, reg_D); cycleLength(4); } // AA    XOR  D
void OPAB(void){ // case  0xAB:
reg_A = XORreg(reg_A, reg_E); cycleLength(4); } // AB    XOR  E
void OPAC(void){ // case  0xAC:
reg_A = XORreg(reg_A, get_rH()); cycleLength(4); } // AC    XOR  H
void OPAD(void){ // case  0xAD:
reg_A = XORreg(reg_A, get_rL()); cycleLength(4); } // AD    XOR  L
void OPAE(void){ // case  0xAE:
reg_A = XORreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // AE    XOR  (HL)

void OPAF(void){ // case  0xAF:
	reg_A = XORreg(reg_A, reg_A);
	cycleLength(4);
} // AF    XOR  A

void OPB0(void){ // case  0xB0:
reg_A = ORreg(reg_A, reg_B);  cycleLength(4); } // B0    OR   B
void OPB1(void){ // case  0xB1:
reg_A = ORreg(reg_A, reg_C);  cycleLength(4); } // B1    OR   C
void OPB2(void){ // case  0xB2:
reg_A = ORreg(reg_A, reg_D);  cycleLength(4); } // B2    OR   D
void OPB3(void){ // case  0xB3:
reg_A = ORreg(reg_A, reg_E);  cycleLength(4); } // B3    OR   E
void OPB4(void){ // case  0xB4:
reg_A = ORreg(reg_A, get_rH()); cycleLength(4); } // B4    OR   H
void OPB5(void){ // case  0xB5:
reg_A = ORreg(reg_A, get_rL()); cycleLength(4); } // B5    OR   L
void OPB6(void){ // case  0xB6:
reg_A = ORreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // B6    OR   (HL)
void OPB7(void){ // case  0xB7:
reg_A = ORreg(reg_A, reg_A); cycleLength(4); } // B7    OR   A
void OPB8(void){ // case  0xB8:
CPreg(reg_A,reg_B); cycleLength(4); } // B8    CP   B
void OPB9(void){ // case  0xB9:
CPreg(reg_A,reg_C); cycleLength(4); } // B9    CP   C
void OPBA(void){ // case  0xBA:
CPreg(reg_A,reg_D); cycleLength(4); } // BA    CP   D
void OPBB(void){ // case  0xBB:
CPreg(reg_A,reg_E); cycleLength(4); } // BB    CP   E
void OPBC(void){ // case  0xBC:
CPreg(reg_A,get_rH()); cycleLength(4); } // BC    CP   H
void OPBD(void){ // case  0xBD:
CPreg(reg_A,get_rL()); cycleLength(4); } // BD    CP   L
void OPBE(void){ // case  0xBE:
CPreg(reg_A,ReadMEM(reg_HL)); cycleLength(8); } // BE    CP   (HL)
void OPBF(void){ // case  0xBF:
CPreg(reg_A,reg_A); cycleLength(4); } // BF    CP   A
void OPC0(void){ // case  0xC0:
	if (!getZ()) {
		reg_PC = ret();
		cycleLength(20);
	} else {
		cycleLength(8);
	}
} // C0    RET  NZ

void OPC1(void){ // case  0xC1:
	put_rBC(pop());
	cycleLength(12);
} // C1    POP  BC

void OPC2(void){ // case  0xC2:
	if (!getZ()) {
		reg_PC = jp(ReadWord(reg_PC));
		cycleLength(16);
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
} // C2    JP   NZ,nnnn

void OPC3(void){ // case  0xC3:
	reg_PC = jp(ReadWord(reg_PC));
	cycleLength(16);
} // C3    JP   nnnn

void OPC4(void){ // case  0xC4:
	if (!getZ()) {
		call();
		reg_PC = ReadWord(reg_PC);
		cycleLength(24);
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
} // C4    CALL NZ,nnnn

void OPC5(void){ // case  0xC5:
push(get_rBC()); cycleLength(16); } // C5    PUSH BC
void OPC6(void){ // case  0xC6:
reg_A = ADDreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // C6    ADD  A,nn
void OPC7(void){ // case  0xC7:
reg_PC = rst(0x0000); cycleLength(16); } // C7    RST  00H

void OPC8(void){ // case  0xC8:
	if (getZ()) {
		reg_PC = ret();
		cycleLength(20);
	} else {
		cycleLength(8);
	}
} // C8    RET  Z

void OPC9(void){ // case  0xC9:
	reg_PC = ret();
	cycleLength(16);
} // C9    RET

void OPCA(void){ // case  0xCA:
	if (getZ()) {
		reg_PC = jp(ReadWord(reg_PC));
		cycleLength(16);
	} else {
		//ReadWord(reg_PC);
		reg_PC += 2;
		cycleLength(12);
	}
}	// CA    JP   Z,nnnn


void OPCB(void){ // case 0xCB:
	currOp = ReadMEM(reg_PC++);
	CBinst[currOp]();
	cycleLength(4);
}

void OPCC(void){ // case  0xCC:

	if (getZ() != 0) {
		call();
		reg_PC = ReadWord(reg_PC);
		 cycleLength(24);
	} else {
		//ReadWord(reg_PC);
		reg_PC += 2;
		cycleLength(12);
	}
} // CC    CALL Z,nnnn

void OPCD(void){ // case  0xCD:
	call();
	reg_PC = ReadWord(reg_PC);
	cycleLength(24);
} // CD    CALL nnnn

void OPCE(void){ // case  0xCE:
reg_A = ADCreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // CE    ADC  A,nn
void OPCF(void){ // case  0xCF:
reg_PC = rst(0x0008); cycleLength(16); } // CF    RST  8

void OPD0(void){ // case  0xD0:
	if (getC() != 1) {
		reg_PC = ret();
		cycleLength(20);
	} else {
		cycleLength(8);
	}
} // D0    RET  NC

void OPD1(void){ // case  0xD1:
	put_rDE(pop());
	cycleLength(10);
} // D1    POP  DE

void OPD2(void){ // case  0xD2:
	if (getC() != 1) {
		reg_PC = jp(ReadWord(reg_PC));
		//reg_PC += 2; // TODO: TEST
		cycleLength(20);
	} else {
		reg_PC += 2;
		cycleLength(8);
	}
}// D2    JP   NC,nnnn

void OPD3(void){
	printf("Incomplete Opcode! D3\n");
	}

void OPD4(void){ // case  0xD4:
	if (getC() != 1) {
		call();
		reg_PC = ReadWord(reg_PC);
		//reg_PC += 2; // TODO: TEST
		cycleLength(24);
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
} // D4    CALL NC,nnnn

void OPD5(void){ // case  0xD5:
push(get_rDE()); cycleLength(16); } // D5    PUSH DE
void OPD6(void){ // case  0xD6:
reg_A = SUBreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // D6    SUB  nn
void OPD7(void){ // case  0xD7:
reg_PC = rst(0x0010); cycleLength(16); } // D7    RST  10H
void OPD8(void){ // case  0xD8:
	if (getC() != 0) {
		reg_PC = ret();
		cycleLength(20);
	} else {
		cycleLength(8);
	}
}// D8    RET  C
void OPD9(void){ // case  0xD9:
reg_PC = ret(); IME = 1; cycleLength(16); } // D9    RETI
void OPDA(void){ // case  0xDA:
	if (getC() != 0) {
		reg_PC = jp(ReadWord(reg_PC));
		//reg_PC += 2; // TODO: TEST
		cycleLength(16);
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
}// DA    JP   C,nnnn
void OPDB(void){
	printf("Incomplete Opcode! DB\n");
	}

void OPDC(void){ // case  0xDC:
	if (getC() != 0) {
		call();
		reg_PC = ReadWord(reg_PC);
		//reg_PC += 2; // TODO: TEST
		cycleLength(24);
		} else {
			reg_PC += 2;
			cycleLength(12);
		}
}// DC    CALL C,nnnn

void OPDD(void){
	printf("Incomplete Opcode! DD\n");
	}

void OPDE(void){ // case  0xDE:
reg_A = SBCreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // DE    SBC  A,nn
void OPDF(void){ // case  0xDF:
reg_PC = rst(0x0018); cycleLength(16); } // DF    RST  18H

void OPE0(void){ // case  0xE0:
	WriteMEM((0xFF00 + ReadMEM(reg_PC++)), reg_A);
	cycleLength(12);
} // E0    LD   ($FF00+nn),A

void OPE1(void){ // case  0xE1:
reg_HL = pop();  cycleLength(12); } // E1    POP  HL

void OPE2(void){ // case  0xE2:
	WriteMEM((0xFF00 + reg_C), reg_A);
	cycleLength(8);
} // E2    LD   ($FF00+C),A

void OPE3(void){
	printf("Incomplete Opcode! E3\n");
	}
void OPE4(void){
	printf("Incomplete Opcode! E4\n");
	}

void OPE5(void){ // case  0xE5:
	push(reg_HL);
	cycleLength(16);
} // E5    PUSH HL

void OPE6(void){ // case  0xE6:

reg_A = ANDreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // E6    AND  nn
void OPE7(void){ // case  0xE7:
reg_PC = rst(0x0020); cycleLength(16); } // E7    RST  20H

void OPE8(void){ // case  0xE8:
	reg_SP += (signed char)(ReadMEM(reg_PC++));
	setN(0); //TODO CHANGED FROM Z()
	cycleLength(16);
} // E8    ADD  SP,dd

void OPE9(void){ // case  0xE9:
reg_PC = reg_HL; cycleLength(4); } // E9    JP   (HL)

void OPEA(void){ // case  0xEA:
	WriteMEM(ReadWord(reg_PC), reg_A);
	reg_PC += 2;
	cycleLength(16);
} // EA    LD   (nnnn),A

void OPEB(void){printf("Incomplete Opcode! EB\n");}
void OPEC(void){printf("Incomplete Opcode! EC\n");}
void OPED(void){printf("Incomplete Opcode! ED\n");}

void OPEE(void){ // case  0xEE:
reg_A = XORreg(ReadMEM(reg_PC++)); cycleLength(8); } // EE    XOR  nn
void OPEF(void){ // case  0xEF:
reg_PC = rst(0x0028);
cycleLength(16);
} // EF    RST  28H

void OPF0(void){ // case  0xF0:
	reg_A = ReadMEM( 0xFF00 | ReadMEM(reg_PC++));
	cycleLength(8);
} // F0    LD   A,($FF00+nn)

void OPF1(void){ // case  0xF1:
	put_rAF(pop());
	cycleLength(12);
} // F1    POP  AF

void OPF2(void){ // case  0xF2:
	reg_A = ReadMEM(0xFF00+reg_C);
	cycleLength(8);
}// F2    LD   A,(C)

void OPF3(void){ // case  0xF3:
	IME = 0;
	cycleLength(4);
} // F3    DI

void OPF4(void){
	printf("Incomplete Opcode! F4\n");
}// 0xF4

void OPF5(void){ // case  0xF5:
	push(get_rAF());
	cycleLength(16);
} // F5    PUSH AF

void OPF6(void){ // case  0xF6:
	reg_A = ORreg(reg_A, ReadMEM(reg_PC++));
	cycleLength(8);
} // F6    OR   nn

void OPF7(void){ // case  0xF7:
	reg_PC = rst(0x0030);
	cycleLength(16);
} // F7    RST  30H

void OPF8(void){ // case  0xF8:
	reg_HL = reg_SP + (signed char)(ReadMEM(reg_PC++));
	cycleLength(12);
} // F8    LD   HL,SP+dd

void OPF9(void){ // case  0xF9:
	reg_SP = reg_HL;
	cycleLength(8);
} //  F9    LD   SP,HL

void OPFA(void){ // case  0xFA:
	reg_A = ReadMEM(ReadWord(reg_PC));
	reg_PC += 2;
	cycleLength(16);
} // FA    LD   A,(nnnn)

void OPFB(void){ // case  0xFB:
	IME = 1;
	cycleLength(4);
} // FB    EI

void OPFC(void){
	#if defined(DEBUG)
	printf("Incomplete Opcode! FC\n");
	#endif
}

void OPFD(void){ // case  0xFD:
	IME = 1;
	cycleLength(4);
} // FD    EI?

void OPFE(void){ // case  0xFE:
	CPreg(reg_A, (BYTE)ReadMEM(reg_PC++));
	cycleLength(8);
} // FE    CP   nn

void OPFF(void){ // case  0xFF:
	reg_PC = rst(0x0038);
	cycleLength(16);
} // FF    RST  38H


// Case CB

void CB00(void) {	reg_B = RLC(reg_B); }  // CB00		RLC B
void CB01(void) {	reg_C = RLC(reg_C); }  // CB01		RLC C
void CB02(void) { 	reg_D = RLC(reg_D); }  // CB02		RLC D
void CB03(void) { 	reg_E = RLC(reg_E); }  // CB03		RLC E
void CB04(void) { 	put_rH(RLC(get_rH())); }  // CB04		RLC H
void CB05(void) {   put_rL(RLC(get_rL())); }  // CB05		RLC L
void CB06(void) {   WriteMEM(reg_HL, RLC(ReadMEM(reg_HL))); } // CB06		RLC (HL)		15	4	2
void CB07(void) {   reg_A = RLC(reg_A); } // CB07		RLC A

void CB08(void) {  reg_B = RRC(reg_B); } // CB08	 	RRC 7,B
void CB09(void) {  reg_C = RRC(reg_C); } // CB09	 	RRC 7,C
void CB0A(void) {  reg_D = RRC(reg_D); } // CB0A	 	RRC 7,D
void CB0B(void) {  reg_E = RRC(reg_E); } // CB0B	 	RRC 7,E
void CB0C(void) {  put_rH(RRC(get_rH())); } // CB0C	 	RRC 7,H
void CB0D(void) {  put_rL(RRC(get_rL())); } // CB0D	 	RRC 7,L
void CB0E(void) {  WriteMEM(reg_HL, RRC(ReadMEM(reg_HL))); } // CB0E	 	RRC 7,(HL)
void CB0F(void) {  reg_A = RRC(reg_A); } // CB0F	 	RRC 7,A

void CB10(void) {  reg_B = RL(reg_B); } // CB10		RL 0,B
void CB11(void) {  reg_C = RL(reg_C); } // CB11		RL 0,C
void CB12(void) {  reg_D = RL(reg_D); } // CB12		RL 0,D
void CB13(void) {  reg_E = RL(reg_E); } // CB13		RL 0,E
void CB14(void) {  put_rH(RL(get_rH())); } // CB14	 	RL 0,H
void CB15(void) {  put_rL(RL(get_rL())); } // CB15	 	RL 0,L
void CB16(void) {  WriteMEM(reg_HL, RL(ReadMEM(reg_HL))); } // CB16	 	RL 0,(HL)
void CB17(void) {  reg_A = RL(reg_A); } // CB17	 	RL 0,A

void CB18(void) {  reg_B = RR(reg_B); } // CB18	 	RR 1,B
void CB19(void) {  reg_C = RR(reg_C); } // CB19	 	RR 1,C
void CB1A(void) {  reg_D = RR(reg_D); } // CB1A	 	RR 1,D
void CB1B(void) {  reg_E = RR(reg_E); } // CB1B	 	RR 1,E
void CB1C(void) {  put_rH(RR(get_rH())); } // CB1C	 	RR 1,H
void CB1D(void) {  put_rL(RR(get_rL())); } // CB1D	 	RR 1,L
void CB1E(void) {  WriteMEM(reg_HL, RR(1, ReadMEM(reg_HL))); } // CB1E	 	RR 1,(HL)
void CB1F(void) {  reg_A = RR(reg_A); } // CB1F	 	RR 1,A

void CB20(void) {  reg_B = SLA(reg_B); } // CB20		SLA 0,B
void CB21(void) {  reg_C = SLA(reg_C); } // CB21		SLA 0,C
void CB22(void) {  reg_D = SLA(reg_D); } // CB22		SLA 0,D
void CB23(void) {  reg_E = SLA(reg_E); } // CB23		SLA 0,E
void CB24(void) {  put_rH(SLA(get_rH())); } // CB24	 	SLA 0,H
void CB25(void) {  put_rL(SLA(get_rL())); } // CB25	 	SLA 0,L
void CB26(void) {  WriteMEM(reg_HL, SLA(ReadMEM(reg_HL))); } // CB26	 	SLA 0,(HL)
void CB27(void) {  reg_A = SLA(reg_A); } // CB27	 	SLA 0,A

void CB28(void) {  reg_B = SRA(reg_B); } // CB28	 	SRA 1,B
void CB29(void) {  reg_C = SRA(reg_C); } // CB29	 	SRA 1,C
void CB2A(void) {  reg_D = SRA(reg_D); } // CB2A	 	SRA 1,D
void CB2B(void) {  reg_E = SRA(reg_E); } // CB2B	 	SRA 1,E
void CB2C(void) {  put_rH(SRA(get_rH())); } // CB2C	 	SRA 1,H
void CB2D(void) {  put_rL(SRA(get_rL())); } // CB2D	 	SRA 1,L
void CB2E(void) {  WriteMEM(reg_HL, SRA(ReadMEM(reg_HL))); } // CB2E	 	SRA 1,(HL)
void CB2F(void) {  reg_A = SRA(reg_A); } // CB2F	 	SRA 1,A

void CB30(void) {  reg_B = SLL(reg_B); } // CB30		SLL 0,B
void CB31(void) {  reg_C = SLL(reg_C); } // CB31		SLL 0,C
void CB32(void) {  reg_D = SLL(reg_D); } // CB32		SLL 0,D
void CB33(void) {  reg_E = SLL(reg_E); } // CB33		SLL 0,E
void CB34(void) {  put_rH(SLL(get_rH())); } // CB34	 	SLL 0,H
void CB35(void) {  put_rL(SLL(get_rL())); } // CB35	 	SLL 0,L
void CB36(void) {  WriteMEM(reg_HL, SLL(ReadMEM(reg_HL))); } // CB36	 	SLL 0,(HL)
void CB37(void) {  reg_A = SLL(reg_A); } // CB37	 	SLL 0,A

void CB38(void) { reg_B = SRL(reg_B); } // CB38	 	SRL 1,B
void CB39(void) {  reg_C = SRL(reg_C); } // CB39	 	SRL 1,C
void CB3A(void) {  reg_D = SRL(reg_D); } // CB3A	 	SRL 1,D
void CB3B(void) {  reg_E = SRL(reg_E); } // CB3B	 	SRL 1,E
void CB3C(void) {  put_rH(SRL(get_rH())); } // CB3C	 	SRL 1,H
void CB3D(void) {  put_rL(SRL(get_rL())); } // CB3D	 	SRL 1,L
void CB3E(void) {  WriteMEM(reg_HL, SRL(ReadMEM(reg_HL))); } // CB3E	 	SRL 1,(HL)
void CB3F(void) {  reg_A = SRL(reg_A); } // CB3F	 	SRL 1,A


void CB40(void) {  BIT(0, reg_B); } // CB40		BIT 0,B
void CB41(void) {  BIT(0, reg_C); } // CB41		BIT 0,C
void CB42(void) {  BIT(0, reg_D); } // CB42		BIT 0,D
void CB43(void) {  BIT(0, reg_E); } // CB43		BIT 0,E
void CB44(void) {  BIT(0, get_rH()); } // CB44	 	BIT 0,H
void CB45(void) {  BIT(0, get_rL()); } // CB45	 	BIT 0,L
void CB46(void) {  BIT(0, ReadMEM(reg_HL)); } // CB46	 	BIT 0,(HL)
void CB47(void) {  BIT(0, reg_A); } // CB47	 	BIT 0,A

void CB48(void) {  BIT(1, reg_B); } // CB48	 	BIT 1,B
void CB49(void) {  BIT(1, reg_C); } // CB49	 	BIT 1,C
void CB4A(void) {  BIT(1, reg_D); } // CB4A	 	BIT 1,D
void CB4B(void) {  BIT(1, reg_E); } // CB4B	 	BIT 1,E
void CB4C(void) {  BIT(1, get_rH()); } // CB4C	 	BIT 1,H
void CB4D(void) {  BIT(1, get_rL()); } // CB4D	 	BIT 1,L
void CB4E(void) {  BIT(1, ReadMEM(reg_HL)); } // CB4E	 	BIT 1,(HL)
void CB4F(void) {  BIT(1, reg_A); } // CB4F	 	BIT 1,A

void CB50(void) {  BIT(2, reg_B); } // CB50		BIT 2,B
void CB51(void) {  BIT(2, reg_C); } // CB51		BIT 2,C
void CB52(void) {  BIT(2, reg_D); } // CB52		BIT 2,D
void CB53(void) {  BIT(2, reg_E); } // CB53		BIT 2,E
void CB54(void) {  BIT(2, get_rH()); } // CB54	 	BIT 2,H
void CB55(void) {  BIT(2, get_rL()); } // CB55	 	BIT 2,L
void CB56(void) {  BIT(2, ReadMEM(reg_HL)); } // CB56	 	BIT 2,(HL)
void CB57(void) {  BIT(2, reg_A); } // CB57	 	BIT 2,A

void CB58(void) {  BIT(3, reg_B); } // CB58	 	BIT 3,B
void CB59(void) {  BIT(3, reg_C); } // CB59	 	BIT 3,C
void CB5A(void) {  BIT(3, reg_D); } // CB5A	 	BIT 3,D
void CB5B(void) {  BIT(3, reg_E); } // CB5B	 	BIT 3,E
void CB5C(void) {  BIT(3, get_rH()); } // CB5C	 	BIT 3,H
void CB5D(void) {  BIT(3, get_rL()); } // CB5D	 	BIT 3,L
void CB5E(void) {  BIT(3, ReadMEM(reg_HL)); } // CB5E	 	BIT 3,(HL)
void CB5F(void) {  BIT(3, reg_A); } // CB5F	 	BIT 3,A

void CB60(void) {  BIT(4, reg_B); } // CB60		BIT 4,B
void CB61(void) {  BIT(4, reg_C); } // CB61		BIT 4,C
void CB62(void) {  BIT(4, reg_D); } // CB62		BIT 4,D
void CB63(void) {  BIT(4, reg_E); } // CB63		BIT 4,E
void CB64(void) {  BIT(4, get_rH()); } // CB64	 	BIT 4,H
void CB65(void) {  BIT(4, get_rL()); } // CB65	 	BIT 4,L
void CB66(void) {  BIT(4, ReadMEM(reg_HL)); } // CB66	 	BIT 4,(HL)
void CB67(void) {  BIT(4, reg_A); } // CB67	 	BIT 4,A

void CB68(void) {  BIT(5, reg_B); } // CB68	 	BIT 5,B
void CB69(void) {  BIT(5, reg_C); } // CB69	 	BIT 5,C
void CB6A(void) {  BIT(5, reg_D); } // CB6A	 	BIT 5,D
void CB6B(void) {  BIT(5, reg_E); } // CB6B	 	BIT 5,E
void CB6C(void) {  BIT(5, get_rH()); } // CB6C	 	BIT 5,H
void CB6D(void) {  BIT(5, get_rL()); } // CB6D	 	BIT 5,L
void CB6E(void) {  BIT(5, ReadMEM(reg_HL)); } // CB6E	 	BIT 5,(HL)
void CB6F(void) {  BIT(5, reg_A); } // CB6F	 	BIT 5,A

void CB70(void) {  BIT(6, reg_B); } // CB70		BIT 6,B
void CB71(void) {  BIT(6, reg_C); } // CB71		BIT 6,C
void CB72(void) {  BIT(6, reg_D); } // CB72		BIT 6,D
void CB73(void) {  BIT(6, reg_E); } // CB73		BIT 6,E
void CB74(void) {  BIT(6, get_rH()); } // CB74	 	BIT 6,H
void CB75(void) {  BIT(6, get_rL()); } // CB75	 	BIT 6,L
void CB76(void) {  BIT(6, ReadMEM(reg_HL)); } // CB76	 	BIT 6,(HL)
void CB77(void) {  BIT(6, reg_A); } // CB77	 	BIT 6,A

void CB78(void) {  BIT(7, reg_B); } // CB78	 	BIT 7,B
void CB79(void) {  BIT(7, reg_C); } // CB79	 	BIT 7,C
void CB7A(void) {  BIT(7, reg_D); } // CB7A	 	BIT 7,D
void CB7B(void) {  BIT(7, reg_E); } // CB7B	 	BIT 7,E
void CB7C(void) {  BIT(7, get_rH()); } // CB7C	 	BIT 7,H
void CB7D(void) {  BIT(7, get_rL()); } // CB7D	 	BIT 7,L
void CB7E(void) {  BIT(7, ReadMEM(reg_HL)); } // CB7E	 	BIT 7,(HL)
void CB7F(void) {  BIT(7, reg_A); } // CB7F	 	BIT 7,A


void CB80(void) {  reg_B = RES(0, reg_B); } // CB80		RES 0,B
void CB81(void) {  reg_C = RES(0, reg_C); } // CB81		RES 0,C
void CB82(void) {  reg_D = RES(0, reg_D); } // CB82		RES 0,D
void CB83(void) {  reg_E = RES(0, reg_E); } // CB83		RES 0,E
void CB84(void) {  put_rH(RES(0, get_rH())); } // CB84	 	RES 0,H
void CB85(void) {  put_rL(RES(0, get_rL())); } // CB85	 	RES 0,L
void CB86(void) {  WriteMEM(reg_HL, RES(0, ReadMEM(reg_HL))); } // CB86	 	RES 0,(HL)
void CB87(void) {  reg_A = RES(0, reg_A); } // CB87	 	RES 0,A

void CB88(void) {  reg_B = RES(1, reg_B); } // CB88	 	RES 1,B
void CB89(void) {  reg_C = RES(1, reg_C); } // CB89	 	RES 1,C
void CB8A(void) {  reg_D = RES(1, reg_D); } // CB8A	 	RES 1,D
void CB8B(void) {  reg_E = RES(1, reg_E); } // CB8B	 	RES 1,E
void CB8C(void) {  put_rH(RES(1, get_rH())); } // CB8C	 	RES 1,H
void CB8D(void) {  put_rL(RES(1, get_rL())); } // CB8D	 	RES 1,L
void CB8E(void) {  WriteMEM(reg_HL, RES(1, ReadMEM(reg_HL))); } // CB8E	 	RES 1,(HL)
void CB8F(void) {  reg_A = RES(1, reg_A); } // CB8F	 	RES 1,A

void CB90(void) {  reg_B = RES(2, reg_B); } // CB90		RES 2,B
void CB91(void) {  reg_C = RES(2, reg_C); } // CB91		RES 2,C
void CB92(void) {  reg_D = RES(2, reg_D); } // CB92		RES 2,D
void CB93(void) {  reg_E = RES(2, reg_E); } // CB93		RES 2,E
void CB94(void) {  put_rH(RES(2, get_rH())); } // CB94	 	RES 2,H
void CB95(void) {  put_rL(RES(2, get_rL())); } // CB95	 	RES 2,L
void CB96(void) {  WriteMEM(reg_HL, RES(2, ReadMEM(reg_HL))); } // CB96	 	RES 2,(HL)
void CB97(void) {  reg_A = RES(2, reg_A); } // CB97	 	RES 2,A

void CB98(void) {  reg_B = RES(3, reg_B); } // CB98	 	RES 3,B
void CB99(void) {  reg_C = RES(3, reg_C); } // CB99	 	RES 3,C
void CB9A(void) {  reg_D = RES(3, reg_D); } // CB9A	 	RES 3,D
void CB9B(void) {  reg_E = RES(3, reg_E); } // CB9B	 	RES 3,E
void CB9C(void) {  put_rH(RES(3, get_rH())); } // CB9C	 	RES 3,H
void CB9D(void) {  put_rL(RES(3, get_rL())); } // CB9D	 	RES 3,L
void CB9E(void) {  WriteMEM(reg_HL, RES(3, ReadMEM(reg_HL))); } // CB9E	 	RES 3,(HL)
void CB9F(void) {  reg_A = RES(3, reg_A); } // CB9F	 	RES 3,A

void CBA0(void) {  reg_B = RES(4, reg_B); } // CBA0		RES 4,B
void CBA1(void) {  reg_C = RES(4, reg_C); } // CBA1		RES 4,C
void CBA2(void) {  reg_D = RES(4, reg_D); } // CBA2		RES 4,D
void CBA3(void) {  reg_E = RES(4, reg_E); } // CBA3		RES 4,E
void CBA4(void) { put_rH(RES(4, get_rH())); } // CBA4	 	RES 4,H
void CBA5(void) {  put_rL(RES(4, get_rL())); } // CBA5	 	RES 4,L
void CBA6(void) {  WriteMEM(reg_HL, RES(4, ReadMEM(reg_HL))); } // CBA6	 	RES 4,(HL)
void CBA7(void) {  reg_A = RES(4, reg_A); } // CBA7	 	RES 4,A

void CBA8(void) {  reg_B = RES(5, reg_B); } // CBA8	 	RES 5,B
void CBA9(void) {  reg_C = RES(5, reg_C); } // CBA9	 	RES 5,C
void CBAA(void) {  reg_D = RES(5, reg_D); } // CBAA	 	RES 5,D
void CBAB(void) {  reg_E = RES(5, reg_E); } // CBAB	 	RES 5,E
void CBAC(void) {  put_rH(RES(5, get_rH())); } // CBAC	 	RES 5,H
void CBAD(void) {  put_rL(RES(5, get_rL())); } // CBAD	 	RES 5,L
void CBAE(void) {  WriteMEM(reg_HL, RES(5, ReadMEM(reg_HL))); } // CBAE	 	RES 5,(HL)
void CBAF(void) {  reg_A = RES(5, reg_A); } // CBAF	 	RES 5,A

void CBB0(void) {  reg_B = RES(6, reg_B); } // CBB0		RES 6,B
void CBB1(void) {  reg_C = RES(6, reg_C); } // CBB1		RES 6,C
void CBB2(void) {  reg_D = RES(6, reg_D); } // CBB2		RES 6,D
void CBB3(void) {  reg_E = RES(6, reg_E); } // CBB3		RES 6,E
void CBB4(void) {  put_rH(RES(6, get_rH())); } // CBB4	 	RES 6,H
void CBB5(void) {  put_rL(RES(6, get_rL())); } // CBB5	 	RES 6,L
void CBB6(void) {  WriteMEM(reg_HL, RES(6, ReadMEM(reg_HL))); } // CBB6	 	RES 6,(HL)
void CBB7(void) {  reg_A = RES(6, reg_A); } // CBB7	 	RES 6,A

void CBB8(void) {  reg_B = RES(7, reg_B); } // CBB8	 	RES 7,B
void CBB9(void) {  reg_C = RES(7, reg_C); } // CBB9	 	RES 7,C
void CBBA(void) {  reg_D = RES(7, reg_D); } // CBBA	 	RES 7,D
void CBBB(void) {  reg_E = RES(7, reg_E); } // CBBB	 	RES 7,E
void CBBC(void) { put_rH(RES(7, get_rH())); } // CBBC	 	RES 7,H
void CBBD(void) {  put_rL(RES(7, get_rL())); } // CBBD	 	RES 7,L
void CBBE(void) {  WriteMEM(reg_HL, RES(7, ReadMEM(reg_HL))); } // CBBE	 	RES 7,(HL)
void CBBF(void) {  reg_A = RES(7, reg_A); } // CBBF	 	RES 7,A

void CBC0(void) {  reg_B = SET(0, reg_B); } // CBC0		SET 0,B
void CBC1(void) {  reg_C = SET(0, reg_C); } // CBC1		SET 0,C
void CBC2(void) {  reg_D = SET(0, reg_D); } // CBC2		SET 0,D
void CBC3(void) {  reg_E = SET(0, reg_E); } // CBC3		SET 0,E
void CBC4(void) {  put_rH(SET(0, get_rH())); } // CBC4	 	SET 0,H
void CBC5(void) {  put_rL(SET(0, get_rL())); } // CBC5	 	SET 0,L
void CBC6(void) {  WriteMEM(reg_HL, SET(0, ReadMEM(reg_HL))); } // CBC6	 	SET 0,(HL)
void CBC7(void) {  reg_A = SET(0, reg_A); } // CBC7	 	SET 0,A

void CBC8(void) { reg_B = SET(1, reg_B); } // CBC8	 	SET 1,B
void CBC9(void) {  reg_C = SET(1, reg_C); } // CBC9	 	SET 1,C
void CBCA(void) {  reg_D = SET(1, reg_D); } // CBCA	 	SET 1,D
void CBCB(void) {  reg_E = SET(1, reg_E); } // CBCB	 	SET 1,E
void CBCC(void) {  put_rH(SET(1, get_rH())); } // CBCC	 	SET 1,H
void CBCD(void) {  put_rL(SET(1, get_rL())); } // CBCD	 	SET 1,L
void CBCE(void) {  WriteMEM(reg_HL, SET(1, ReadMEM(reg_HL))); } // CBCE	 	SET 1,(HL)
void CBCF(void) {  reg_A = SET(1, reg_A); } // CBCF	 	SET 1,A

void CBD0(void) {  reg_B = SET(2, reg_B); } // CBD0		SET 2,B
void CBD1(void) {  reg_C = SET(2, reg_C); } // CBD1		SET 2,C
void CBD2(void) {  reg_D = SET(2, reg_D); } // CBD2		SET 2,D
void CBD3(void) {  reg_E = SET(2, reg_E); } // CBD3		SET 2,E
void CBD4(void) {  put_rH(SET(2, get_rH())); } // CBD4	 	SET 2,H
void CBD5(void) {  put_rL(SET(2, get_rL())); } // CBD5	 	SET 2,L
void CBD6(void) {  WriteMEM(reg_HL, SET(2, ReadMEM(reg_HL))); } // CBD6	 	SET 2,(HL)
void CBD7(void) {  reg_A = SET(2, reg_A); } // CBD7	 	SET 2,A

void CBD8(void) {  reg_B = SET(3, reg_B); } // CBD8	 	SET 3,B
void CBD9(void) {  reg_C = SET(3, reg_C); } // CBD9	 	SET 3,C
void CBDA(void) {  reg_D = SET(3, reg_D); } // CBDA	 	SET 3,D
void CBDB(void) {  reg_E = SET(3, reg_E); } // CBDB	 	SET 3,E
void CBDC(void) {  put_rH(SET(3, get_rH())); } // CBDC	 	SET 3,H
void CBDD(void) {  put_rL(SET(3, get_rL())); } // CBDD	 	SET 3,L
void CBDE(void) {  WriteMEM(reg_HL, SET(3, ReadMEM(reg_HL))); } // CBDE	 	SET 3,(HL)
void CBDF(void) {  reg_A = SET(3, reg_A); } // CBDF	 	SET 3,A

void CBE0(void) { reg_B = SET(4, reg_B); } // CBE0		SET 4,B
void CBE1(void) {  reg_C = SET(4, reg_C); } // CBE1		SET 4,C
void CBE2(void) {  reg_D = SET(4, reg_D); } // CBE2		SET 4,D
void CBE3(void) {  reg_E = SET(4, reg_E); } // CBE3		SET 4,E
void CBE4(void) {  put_rH(SET(4, get_rH())); } // CBE4	 	SET 4,H
void CBE5(void) {  put_rL(SET(4, get_rL())); } // CBE5	 	SET 4,L
void CBE6(void) {  WriteMEM(reg_HL, SET(4, ReadMEM(reg_HL))); } // CBE6	 	SET 4,(HL)
void CBE7(void) {  reg_A = SET(4, reg_A); } // CBE7	 	SET 4,A

void CBE8(void) {  reg_B = SET(5, reg_B); } // CBE8	 	SET 5,B
void CBE9(void) {  reg_C = SET(5, reg_C); } // CBE9	 	SET 5,C
void CBEA(void) {  reg_D = SET(5, reg_D); } // CBEA	 	SET 5,D
void CBEB(void) {  reg_E = SET(5, reg_E); } // CBEB	 	SET 5,E
void CBEC(void) {  put_rH(SET(5, get_rH())); } // CBEC	 	SET 5,H
void CBED(void) {  put_rL(SET(5, get_rL())); } // CBED	 	SET 5,L
void CBEE(void) {  WriteMEM(reg_HL, SET(5, ReadMEM(reg_HL))); } // CBEE	 	SET 5,(HL)
void CBEF(void) {  reg_A = SET(5, reg_A); } // CBEF	 	SET 5,A

void CBF0(void) {  reg_B = SET(6, reg_B); } // CBF0		SET 6,B
void CBF1(void) {  reg_C = SET(6, reg_C); } // CBF1		SET 6,C
void CBF2(void) {  reg_D = SET(6, reg_D); } // CBF2		SET 6,D
void CBF3(void) {  reg_E = SET(6, reg_E); } // CBF3		SET 6,E
void CBF4(void) {  put_rH(SET(6, get_rH())); } // CBF4	 	SET 6,H
void CBF5(void) {  put_rL(SET(6, get_rL())); } // CBF5	 	SET 6,L
void CBF6(void) {  WriteMEM(reg_HL, SET(6, ReadMEM(reg_HL))); } // CBF6	 	SET 6,(HL)
void CBF7(void) { reg_A = SET(6, reg_A); } // CBF7	 	SET 6,A

void CBF8(void) {  reg_B = SET(7, reg_B); } // CBF8	 	SET 7,B
void CBF9(void) {  reg_C = SET(7, reg_C); } // CBF9	 	SET 7,C
void CBFA(void) {  reg_D = SET(7, reg_D); } // CBFA	 	SET 7,D
void CBFB(void) {  reg_E = SET(7, reg_E); } // CBFB	 	SET 7,E
void CBFC(void) {  put_rH(SET(7, get_rH())); } // CBFC	 	SET 7,H
void CBFD(void) {  put_rL(SET(7, get_rL())); } // CBFD	 	SET 7,L
void CBFE(void) {  WriteMEM(reg_HL, SET(7, ReadMEM(reg_HL))); } // CBFE	 	SET 7,(HL)
void CBFF(void) {  reg_A = SET(7, reg_A); } // CBFF	 	SET 7,A

