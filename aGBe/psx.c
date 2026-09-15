// PSX.c TODO: Write File Description Here

// includes ////////////////////////////////////////////////////
#include <sys/types.h>
#include <sys/file.h>
#include <libapi.h>
#include <kernel.h>
#include <libetc.h>
#include <libcd.h>
#include <libgte.h>
#include <libgpu.h>
#include <libgs.h>
#include <strings.h>
#include <stdio.h>
#include "main.h"
#include "pad.h"
#include "gui.h"
//#include "mem.h"
#include "psx.h"

// The one real (defining) declaration of pad/lastpad and ROM - every other
// file that needs them gets `extern` declarations from pad.h/psx.h instead.
// See the BUG FIX notes in those headers for why this matters on any
// compiler that isn't defaulting to old-style "common symbol" linking.
u_long pad, lastpad;
BYTE *ROM;





// functions ////////////////////////////////////////////////////
void drawTIM(int timNumber){

	GsSortSprite(&pic[timNumber], &myOT[activeBuffer], 0);
}

void init_PSX(void){
	// ResetCallback();
	// AutoDetect Video Mode
	printf("Initializing PSX...\n");

	if (*(char *)0xbfc7ff52=='E')
		SetVideoMode(MODE_PAL);
	else
		SetVideoMode(MODE_NTSC);



	ResetGraph(0);
	GsInitGraph(SCREEN_WIDTH, SCREEN_HEIGHT, GsINTER|GsOFSGPU, 0,0);
	GsDefDispBuff(0,0,0,SCREEN_HEIGHT);
	myOT[0].length=OT_LENGTH;
	myOT[1].length=OT_LENGTH;
	myOT[0].org=myOT_TAG[0];
	myOT[1].org=myOT_TAG[1];
	GsClearOt(0,0,&myOT[0]);
	GsClearOt(0,0,&myOT[1]);

	//_96_remove();
	// Initialize pad
	PadInit(0);
	pad=0;
	//_96_init();



	FntLoad(960, 256);
	games = FntOpen(32, 35,256,164,0,1024);
	SetDispMask(1);  /* start display */

}


void PrepScreen(){
	activeBuffer = GsGetActiveBuff();
	GsSetWorkBase((PACKET*)GpuPacketArea[activeBuffer]);
	GsClearOt(0, 0, &myOT[activeBuffer]);

}

void initFont(){


myFont[0].order =  " !!#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[!]^_'abcdefghijklmnopqrstuvwxyz{|}~^";
myFont[0].u = 13;
myFont[0].v = 16;
myFont[0].h = 6;
myFont[0].w = 16;
myFont[0].timNum = 1;


myFont[1].order = "0123456789:;<=>? ABCDEFGHIJKLMNOPQRSTUVWXYZ[!]^_'abcdefghijklmnopqrstuvwxyz{|}!!";
myFont[1].u = 50;
myFont[1].v = 33;
myFont[1].h = 5;
myFont[1].w = 16;
myFont[1].timNum = 2;
}

void loadFonts(GsIMAGE *im, int fntNum){
int bits, widthCompression;
bits=im->pmode&0x03;
if (bits==0) widthCompression=4;
	else if (bits==1) widthCompression=2;
	else if (bits==2) widthCompression=1;
	else {}
	myRect.x = im->px;
	myRect.y = im->py;
	myRect.w = im->pw;
	myRect.h = im->ph;
	LoadImage( &myRect, im->pixel );
	myFont[fntNum].fnttxt.attribute = (bits<<24);
	myFont[fntNum].fnttxt.x = SCREEN_WIDTH/2-((im->pw*widthCompression)/2);
	myFont[fntNum].fnttxt.y = SCREEN_HEIGHT/2- im->ph/2;
	myFont[fntNum].fnttxt.w = im->pw*widthCompression;
	myFont[fntNum].fnttxt.h = im->ph;
	myFont[fntNum].fnttxt.tpage=GetTPage(bits, 0, im->px, im->py);
	myFont[fntNum].fnttxt.u=(im->px%64) * widthCompression;
	myFont[fntNum].fnttxt.v=im->py%256;

	if (bits==0||bits==1) {
		myRect.x = im->cx;
		myRect.y = im->cy;
		myRect.w = im->cw;
		myRect.h = im->ch;
		LoadImage( &myRect, im->clut );
		myFont[fntNum].fnttxt.cx=im->cx;
		myFont[fntNum].fnttxt.cy=im->cy;
	}
	myFont[fntNum].fnttxt.r=myFont[fntNum].fnttxt.b=myFont[fntNum].fnttxt.g=128;
	myFont[fntNum].fnttxt.scalex=ONE;
	myFont[fntNum].fnttxt.scaley=ONE;
	myFont[fntNum].fnttxt.rotate=0*ONE;

}

void writeFont(char *ch, int FntNumba, int x, int y) {
int j,k;

pic[myFont[FntNumba].timNum].y = y;
pic[myFont[FntNumba].timNum].x = x;
pic[myFont[FntNumba].timNum].w = myFont[FntNumba].u;
pic[myFont[FntNumba].timNum].h = myFont[FntNumba].v;
for (j=0;j<strlen(ch);j++) {
for (k=0;k<strlen(myFont[FntNumba].order);k++){

if (myFont[FntNumba].order[k]==ch[j]){

  pic[myFont[FntNumba].timNum].u =  myFont[FntNumba].fnttxt.u +((k) % myFont[FntNumba].w  ) * myFont[FntNumba].u;
  pic[myFont[FntNumba].timNum].v =  myFont[FntNumba].fnttxt.v +(int)( (k) / myFont[FntNumba].w  ) * myFont[FntNumba].v;
  pic[myFont[FntNumba].timNum].x = pic[myFont[FntNumba].timNum].x + myFont[FntNumba].u;

  GsSortSprite(&pic[myFont[FntNumba].timNum], &myOT[activeBuffer], 0);
}
}
}

}

void RenderWorld(BYTE re, BYTE gr, BYTE bl){
	//FntPrint(games, "Beta Version");

    DrawSync(0);
 	VSync(0);
	GsSwapDispBuff();
	GsSortClear(re, gr, bl, &myOT[activeBuffer]);
	GsDrawOt(&myOT[activeBuffer]);
	FntFlush(games);
}

void LoadTim(GsIMAGE *im, GsSPRITE *sp){
	int bits, widthCompression;

	bits=im->pmode&0x03;
	if (bits==0) widthCompression=4;		        //tim is a 4-bit picture
	else if (bits==1) widthCompression=2;		//tim is a 8-bit picture
	else if (bits==2) widthCompression=1;		//tim is a 16-bit picture
	else { exit(-1); }

	myRect.x = im->px;
	myRect.y = im->py;
	myRect.w = im->pw;
	myRect.h = im->ph;
	LoadImage( &myRect, im->pixel );

	sp->attribute = (bits<<24);
	sp->x = SCREEN_WIDTH/2-((im->pw*widthCompression)/2);
	sp->y = SCREEN_HEIGHT/2-im->ph/2;
	sp->w = im->pw*widthCompression;
	sp->h = im->ph;
	sp->tpage=GetTPage(bits, 0, im->px, im->py);
	sp->u= (im->px%64) * widthCompression; //0
	sp->v= im->py%256; //0
	if (bits==0||bits==1) {

		myRect.x = im->cx;
		myRect.y = im->cy;
		myRect.w = im->cw;
		myRect.h = im->ch;
		LoadImage( &myRect, im->clut );
		sp->cx=im->cx;
		sp->cy=im->cy;
	}

	sp->r=128;
	sp->b=128;
	sp->g=128;
	sp->scalex=ONE;
	sp->scaley=ONE;
	sp->mx=(im->pw*widthCompression)/2;
	sp->my=im->ph/2;
	sp->rotate=0*ONE;

}


void Change_Res(){

	 	ResetCallback();

		// Initialize graphics
		//width, height,
		GsInitGraph(256,240, GsINTER|GsOFSGPU, 1, 0);
		GsDefDispBuff(0, 0, 0, 0);
		ResetGraph(1);
}


void Draw_Buffer(int *screenBuffer){
		/*
			Video Notes:
			------------
			Using a sprite for blitting is the fastest by far.

			SPRT is about
			x2 POLY_F4
			x4 POLY_G4
			x10+ doing each pixel

			Using SPRT also enables the Texture Cache which speeds things up a lot.  Although I don't really understand it.

			Also streching the sprite to full screen wouldn't take up  time since its done by the GPU.

		Doing like a memmove() would also be alot fast then use for(),
		don't know how pix is set up, be really cool if you could wrap a sprite header around it,
		then nothing would have to be done.
		-ceddy
	*/
	//#if defined(DEBUG)
	//	Draw_Buffer_SPRT(screenBuffer);
	//#else
		Draw_Buffer_Pixel_Blitting(screenBuffer);
	//#endif


}
void Draw_Buffer_SPRT(int *screenBuffer){
	// New Drawing routines.
	// This shouldnt be initialized each time.
	RECT scrRect, clutRect;
	GsSPRITE *myScreen;
	GsIMAGE myImg;
	int z;
	int p;
	WORD tmpPixel;
	WORD myCLUT[4];




	// Need to convert screen buffer to texture format 4bits per colour mapped to clut


    WORD Buffer [ 144 * 160 / 4 ];


	// Make CLUT
	myCLUT[0] = 0x0000;
	myCLUT[1] = 0x5555;
	myCLUT[2] = 0xAAAA;
	myCLUT[3] = 0xFFFF;

	LoadImage(&clutRect, (u_long *)&(myCLUT));

	for (z=0; z < (144 * 160 / 4); z++){
		tmpPixel = 0x0000;
		for (p = 0; p < 4; p++) {
			tmpPixel |= (screenBuffer[(z * 4) + p] & 0x0F) << (p * 4);
		}
		Buffer[z]=tmpPixel;
	}




	myImg.pmode = 0; // 4bits per pixel

	myImg.px = 320;
	myImg.py = 60;
	myImg.pw = 144;
	myImg.ph = 160;
	myImg.pixel = Buffer;
	myImg.cx = 320;
	myImg.cy = 160;
	myImg.cw = 4;
	myImg.ch = 1;
	myImg.clut = GetClut(clutRect.x, clutRect.y);

	//LoadTim(&myImg, &myScreen);


	LoadImage(&scrRect, (u_long *)&(Buffer));
	GsSortSprite(myScreen, &myOT[activeBuffer], 0);

		/*
		Hey gxd heres something that might be faster for a blitter.

		VRAM is faster than RAM so better to process image in there.
		Also better to have GPU process to free up CPU.
		I think screenBuffer is 1 pixel per byte, if you could change
		>that to 2 pixel per byte(4bit+4bit) transfers would be twice as fast.

		Just the general idea.
		RECT scrRect, clutRect;
		GsSPRITE mySprite;
		GsImage myImg;

		myImg.pmode = 1 // 8bit clut, change to 4bit x2 faster
		myImg.px = 320;
		// Frame buffer storage, change to whatever

		myImg.py = 0;
		myImg.pw = 160; // Width & Height
		myImg.ph = 140;
		myImg.*pixel = screenBuffer // Pointer to pixel data
		myImg.cx = 320 // CLUT storage in FB
		myImg.cy = 140
		myImg.cw = 16
		myImg.ch = 1;
		myImg.*clut = myCLUT // CLUT data

		InitSprite(&myImg, &mySprite); // Sets up sprite info
		LoadImage(&clutRect, myCLUT); // Load clut to FB
		//Display Loop
		...
		LoadImage(&scrRect, screenBuffer);
		// Load updated screen
		GsSortSprite(mySprite, &myOT[activeBuffer], 0); // Put it in OT
		...
	*/



}
void Draw_Buffer_Pixel_Blitting(int *screenBuffer){


	int i, j;
	setWH(&pix, 1.5, 1.5);
	for (i = 0; i < 144; i++) {
		for (j = 0; j < 160; j++){
			SetTile(&pix);
			pix.x0 = (j * pix.w) + 70 ;
			pix.y0 = (i * pix.h) + 25 ;
			pix.r0 = pix.g0 = pix.b0 = (u_char)((u_char)(3-screenBuffer[(i * 160) + j]) * 0x3F);
			DrawPrim(&pix);
			//AddPrim(&myOT,&pix);
			// I would like to move all of this to the psx.c file with a pointer to the screen Buffer
		}

	}
	//DR_MODE dr_mode;
	//SPRT sprt;

	//SetDrawMode(*&dr_mode, 0, 0, GetTPage(2,0,640,0), 0);
	//SetSprt(&sprt);
	//setXYWH(&sprt, 100, 100,160,144);

	//AddPrim(&myOT[activeBuffer], &sprt);
	//AddPrim(&myOT[activeBuffer], &dr_mode);


}

void LoadFiles(void){
	CdInit();
	LoadRombank();

}

int LoadRombank(void){
	int i;
	gnumber = 0;

	//FntPrint(games, "               Loading Rombank TOC...");
	CdSearchFile(&cdf, "\\AGBEBANK.BIN;1");
	filePos = CdPosToInt(&cdf.pos);
	CdControl(CdlSetloc, (u_char *)&(cdf.pos), 0);

	CdRead( 64, (u_long *)&(list), 1);
	i = 0;
	while (CdReadSync(1, 0)) {
		VSync(0);
		i++;
		if (i > 1000){
			//Somthing's Wrong
			CdReset(0);
			return 0;
		}
	}

	for( i=0; i<4096; i++) {
		if( list[i].name[0] == 0xff ) break;
		gnumber++;
	}

	printf("Loading Games Complete...Found:%d\n",gnumber);
	return gnumber;
}

void LoadImageFromCD(char *name, int N, int X, int Y) {
	// Reads TIM File from CD and stores it into pic[N]

	CdSearchFile(&cdf, name);
	CdControl(CdlSetloc, (u_char *)&(cdf.pos), 0);
	CdRead( 76, (u_long *)&(timbuffer), 1);
	while (CdReadSync(1, 0)) {
		VSync(0);
	}
	GsGetTimInfo((u_long *)(timbuffer+4), &myTim);
	LoadTim(&myTim, &pic[N]);
	pic[N].x = X;
	pic[N].y = Y;

}

int GameSelectMenu(void) {
	// This really belongs in gui.c
		int GameNumber = 0;
		int done = 0;
		//int MAXLINES=14;
		int N;
		int k;
		char title[29];
		title[29] = 0x00;

		while(!done){
			PrepScreen();
			//drawTIM(3);
			//drawTIM(4);
			//drawTIM(5);
			//drawTIM(6);
			pad = PadRead(0);
			FntPrint(games, "               %d of %d\n\n", GameNumber+1, gnumber);
			if (pad != lastpad){
				if(pad & Pad1Start) { // picked game
					done = 1;
				} else if(pad & Pad1Up) {
					//if (GameNumber > 0)  {
						GameNumber --;
						if (GameNumber < 0){
							GameNumber = gnumber + GameNumber; // Note: '+' because GameNumber is negative.
						}
					//}

				} else if (pad & Pad1Down) {
				   // if (GameNumber < gnumber - 1 ) {
						GameNumber++;
						GameNumber %= gnumber;
					//}

				} else if (pad & Pad1x) {
					// TODO: Fix This!
					//DispInfo(GameNumber);
					//RenderWorld(0x00, 0x00, 0x00);
					//while((pad & Pad1x)){
					//	pad = PadRead(0);
					//}PrepScreen();
				} else if (pad & Pad1R1){
					GameNumber+=10;
					GameNumber%=gnumber;
				} else if (pad & Pad1L1){
					GameNumber-=10;
					if (GameNumber < 0){
						GameNumber = gnumber + GameNumber; // Note: '+' because GameNumber is negative.
					}
				}
			lastpad = pad;
			}



					for( k=7; k>0; k--)
						if( (GameNumber-k) >= 0) {
							for( N=0; N<28; N++) title[N] = list[GameNumber-k].name[N];
							FntPrint(games,"%s\n", title);
						} else FntPrint(games, "\n");

					FntPrint(games,"+   -   -    ..    -   -   +\n");

					for( k=0; k<28; k++) title[k] = list[GameNumber].name[k];
					FntPrint(games,"%s \n", title);

					FntPrint(games,"+   -   -    ..    -   -   +\n");

					for( k=1; k<=7; k++)
						if( (GameNumber+k) < gnumber) {
							for( N=0; N<28; N++) title[N] = list[GameNumber + k].name[N];
							FntPrint(games,"%s\n", title);
			} else FntPrint(games, "\n");

				drawTIM(nGUIMENU_TL);
				drawTIM(nGUIMENU_TR);
				drawTIM(nGUIMENU_BR);
				drawTIM(nGUIMENU_BL);
				RenderWorld(0xCC, 0xCC, 0xCC);
					while ((pad & Pad1Down) || (pad & Pad1Up)) {
							pad = PadRead(0);
				}

	}
	return GameNumber;
}

void 	DispInfo(int GameNumber) {

// Displays info from romheader,  so far CART TYPE, ROM size, SRAM size,
// SGB, country
// Will add Licensee when I feel like typing 200 names

	int p;
	struct	RomHeader	head;
	u_long	sp;
	int i;


	CdSearchFile(&cdf, "\\AGBEBANK.BIN;1");

	sp = CdPosToInt(&cdf.pos);
	sp = sp + list[GameNumber].loc - 30;
	CdIntToPos( sp, &cdf.pos);

	CdControl(CdlSetloc, (u_char *)&(cdf.pos), 0);
	CdRead( 1, (u_long *)&(head), 1);
	while (CdReadSync(1, 0)) {
		VSync(0);
	}


	FntPrint(games, "\n\n");
	FntPrint(games, "Title:	%s\n", head.title);
        FntPrint(games, "Cart Type: ");
	if(head.cart == 0x00) FntPrint(games,"ROM");
	if(head.cart == 0x01) FntPrint(games,"MBC1");
	if(head.cart == 0x02) FntPrint(games,"MBC1+RAM");
	if(head.cart == 0x03) FntPrint(games,"MBC1+RAM+BATTERY");
	if(head.cart == 0x05) FntPrint(games,"MBC2");
 	if(head.cart == 0x06) FntPrint(games,"MBC2+BATTERY");
	if(head.cart == 0x08) FntPrint(games,"ROM+RAM");
	if(head.cart == 0x09) FntPrint(games,"ROM+RAM+BATTERY");
	if(head.cart == 0x0B) FntPrint(games,"MMM01");
	if(head.cart == 0x0C) FntPrint(games,"MMM01+RAM");
  	if(head.cart == 0x0D) FntPrint(games,"MMM01+RAM+BATTERY");
	if(head.cart == 0x0F) FntPrint(games,"MBC3+TIMER+BATTERY");
	if(head.cart == 0x10) FntPrint(games,"MBC3+TIMER+RAM+BATTERY");
	if(head.cart == 0x11) FntPrint(games,"MBC3");
	if(head.cart == 0x12) FntPrint(games,"MBC3+RAM");
 	if(head.cart == 0x13) FntPrint(games,"MBC3+RAM+BATTERY");
	if(head.cart == 0x15) FntPrint(games,"MBC4");
	if(head.cart == 0x16) FntPrint(games,"MBC4+RAM");
	if(head.cart == 0x17) FntPrint(games,"MBC4+RAM+BATTERY");
	if(head.cart == 0x19) FntPrint(games,"MBC5");
        if(head.cart == 0x1A) FntPrint(games,"MBC5+RAM");
	if(head.cart == 0x1B) FntPrint(games,"MBC5+RAM+BATTERY");
	if(head.cart == 0x1C) FntPrint(games,"MBC5+RUMBLE");
	if(head.cart == 0x1D) FntPrint(games,"MBC5+RUMBLE+RAM");
	if(head.cart == 0x1E) FntPrint(games,"MBC5+RUMBLE+RAM+BATTERY");
        if(head.cart == 0xFC) FntPrint(games,"POCKET CAMERA");
	if(head.cart == 0xFD) FntPrint(games,"Bandai TAMA5");
	if(head.cart == 0xFE) FntPrint(games,"HuC3");
	if(head.cart == 0xFF) FntPrint(games,"HuC1+RAM+BATTERY");
        FntPrint(games, "\n");
	FntPrint(games, "Rom  Size: ");
        if(head.romsize == 0x00) FntPrint(games,"32k");
	if(head.romsize == 0x01) FntPrint(games,"64k");
	if(head.romsize == 0x02) FntPrint(games,"128k");
	if(head.romsize == 0x03) FntPrint(games,"256k");
	if(head.romsize == 0x04) FntPrint(games,"512k");
 	if(head.romsize == 0x05) FntPrint(games,"1024k");
	if(head.romsize == 0x06) FntPrint(games,"2048k");
	if(head.romsize == 0x07) FntPrint(games,"4096k");
	FntPrint(games, "\n");
 	FntPrint(games, "SRam Size: ");
        if(head.sramsize == 0x00) FntPrint(games,"0k");
	if(head.sramsize == 0x01) FntPrint(games,"2k");
	if(head.sramsize == 0x02) FntPrint(games,"8k");
	if(head.sramsize == 0x03) FntPrint(games,"32k");
	FntPrint(games, "\n");
 	FntPrint(games, "Super GB: ");
        if(head.sgb == 0x03) FntPrint(games,"Yes");
	else FntPrint(games, "No");
	FntPrint(games, "\n");
        FntPrint(games, "Country: ");
        if(head.country == 0x00) FntPrint(games,"Japan");
	if(head.country == 0x01) FntPrint(games,"Non-Japan");
	FntPrint(games, "\n\n");

	FntPrint(games, "Cd size: %d Bytes\n", list[GameNumber].size);
	FntPrint(games, "Cd Offset: %d sectors\n", list[GameNumber].loc);

	FntPrint(games,"\n\nPSX BIOS -> ");
	if(*(char *)0xbfc7ff52 == 'E')FntPrint     ("PAL  Europe\n\n");
	else if(*(char *)0xbfc7ff52 == 'A')FntPrint("NTSC America\n\n");
	else if(*(char *)0xbfc7ff52 == 'J')FntPrint("NTSC Japan\n\n");
	else if(*(char *)0xbfc7ff52 ==  0 )FntPrint("SCPH1000?\n\n");
	else FntPrint                              ("Unknown Bios!\n\n");

	for (p = 0; p < 11; p++)
		if(*(char *)(0xbfc7ff32+p) > 31)
			FntPrint("%c", *(char *)(0xbfc7ff32+p));
	FntPrint("\n");
	for (p = 11; p < 23; p++)
		if(*(char *)(0xbfc7ff32+p) > 31)
			FntPrint("%c", *(char *)(0xbfc7ff32+p));
	FntPrint("\n");
	for (p = 23; p < 33; p++)
		if(*(char *)(0xbfc7ff32+p) > 31)
			FntPrint("%c", *(char *)(0xbfc7ff32+p));
	FntPrint("\n");


}


int LoadGame(int GameNumber) {
		u_long	sp;

		//struct	RomHeader	head;
		//int ROMSize;
		int i;

		#if defined(DEBUG)
		printf("Loading Game Number %d\n", GameNumber);
		printf("Rom Size: %d\n", list[GameNumber].size);
		printf("Rom Loc: %d\n",  list[GameNumber].loc);
		#endif

		CdSearchFile(&cdf, "\\AGBEBANK.BIN;1");
		#if defined(DEBUG)
		printf("Found ROMBank File...\n");
		#endif



		/*sp = filePos + list[GameNumber].loc;
		CdIntToPos( sp, &cdf.pos);

		CdControl(CdlSetloc, (u_char *)&(cdf.pos), 0);
		CdRead( 1, (u_long *)&(head), 1);
		while (CdReadSync(1, 0)) {
			VSync(0);
		}

		if(head.romsize == 0x00){ ROMSize = 32; }
		else if(head.romsize == 0x01) { ROMSize = 64;   }
		else if(head.romsize == 0x02) {  ROMSize = 128; }
		else if(head.romsize == 0x03) { ROMSize = 256;  }
		else if(head.romsize == 0x04) { ROMSize = 512;  }
		else if(head.romsize == 0x05) { ROMSize = 1024; }
		else if(head.romsize == 0x06) { ROMSize = 2048; }
		else if(head.romsize == 0x07) { ROMSize = 4096; }
		else { ROMSize = 32; printf("ERROR"); }


		// Use the same CDPos
		//CdSearchFile(&cdf, "\\AGBEBANK.BIN;1");
		*/
		//sp = CdPosToInt(&cdf.pos);
		ROM    = (u_long *)malloc(list[GameNumber].size * sizeof(BYTE));



		sp = filePos + (u_long)(list[GameNumber].loc/2048);
		CdIntToPos( sp, &cdf.pos);
		#if defined(DEBUG)
			printf("File pointer Sector: %d\n", filePos);
			printf("Rom Loc:         %d\n", list[GameNumber].loc);
			printf("Seeking to LOC %d:\n", sp);
		#endif

		CdControl(CdlSetloc, (u_char *)&(cdf.pos), 0);

		CdRead( list[GameNumber].size / 2048, (ROM), 1);
		i = 0;
		while (CdReadSync(1, 0)) {
			VSync(0);
			i++;
			if (i > 1000){
				//Somthing's Wrong

				CdReset(0);
				return 0;
			}
		}
	    printf("Finished Loading Rom into Memory...\n");

		return 1;


}
