// GUI TODO: Write File Description.

// includes ////////////////////////////////////////////////////
#include <sys/types.h>
#include <sys/file.h>
#include <libetc.h>
#include <libgte.h>
#include <libgpu.h>
#include <libgs.h>
#include <libcd.h>
#include <libsn.h>
#include <kernel.h>

#include <strings.h>
#include "main.h"
#include "gui.h"
#include "pad.h"
#include "psx.h"
#include "mem.h"

// globals ////////////////////////////////////////////////////
char CREDITSdata[41][30]={  "CREDITS",
							"",
							"aGBe",
							"a gameboy emulator",
							"",
							"www.gamebase.ca/agbe/",
							"",
							"",
							"Programmers",
							"gxd",
							"Skitchin"
							"Sparrow",
							"ceddy",
							"",
							"Website Guru",
							"[vEX]",
							"",
							"Special Thanks to",
							"[vEX]",
							"justice7",
							"Gamebase.ca",
							"Allan [imbNES]",
							"",
							"",
							"Thanks for Playing",
							"",
							"Disclaimer",
							"No persons related to",
							"this project is held ",
							"accountable for any ",
							"damaged systems, TVs,",
							"components, TVs or lives.",
							"Please use at your own risk",
							"We do not condone piracy",
							"of any gameboy games!",
							"We are not affiliated",
							"with Nintendo or Sony!",
							"",
							"Thanks for your support",
							"gxd",
							"(c) 2002"};
RECT myRect;  // TODO: Move This

//TODO: Move These!
int done;
int c;
int menu_pos;
int i;
int GameNumber;

// defines ////////////////////////////////////////////////////
#define CREDITSLINES 41



// functions ////////////////////////////////////////////////////

void initImages(void){
	// This Function will load all the images in memory

	#if defined(DEBUG)
		printf("Loading Splash Screen..\n");
	#endif

	// TODO: Move the Splash Screen to its own function, and add UnLoad
	GsGetTimInfo((u_long *)(SPLASHL_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nSPLASH_L]);

	GsGetTimInfo((u_long *)(SPLASHR_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nSPLASH_R]);

	// Set Positions
	pic[nSPLASH_L].x=80;
	pic[nSPLASH_L].y=120;
	pic[nSPLASH_R].x=240;
	pic[nSPLASH_R].y=120;


	#if defined(DEBUG)
	printf("Loading TIMs...");
	#endif

	GsGetTimInfo((u_long *)(GUI_TPAGE_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nGUIMENU_BL]);
	GsGetTimInfo((u_long *)(GUI_TPAGE_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nGUIMENU_TL]);
	GsGetTimInfo((u_long *)(GUI_TPAGE_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nGUIMENU_BR]);
	GsGetTimInfo((u_long *)(GUI_TPAGE_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nGUIMENU_TR]);

	pic[nGUIMENU_TL].x=80;
	pic[nGUIMENU_TL].y=62;
	pic[nGUIMENU_TL].w=160;
	pic[nGUIMENU_TL].h=46;
	pic[nGUIMENU_TL].v=30;

	pic[nGUIMENU_TR].x=240;
	pic[nGUIMENU_TR].y=62;
	pic[nGUIMENU_TR].w=160;
	pic[nGUIMENU_TR].h=46;
	pic[nGUIMENU_TR].v=77;

	pic[nGUIMENU_BR].x=240;
	pic[nGUIMENU_BR].y=348;
	pic[nGUIMENU_BR].w=160;
	pic[nGUIMENU_BR].h=46;
	pic[nGUIMENU_BR].v=171;

	pic[nGUIMENU_BL].x=80;
	pic[nGUIMENU_BL].y=348;
	pic[nGUIMENU_BL].w=160;
	pic[nGUIMENU_BL].h=46;
	pic[nGUIMENU_BL].v=124;

	GsGetTimInfo((u_long *)(GUI_TPAGE_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nGUIGAMES]);
	GsGetTimInfo((u_long *)(GUI_TPAGE_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nGUIMEMCARD]);
	GsGetTimInfo((u_long *)(GUI_TPAGE_TIM+4), &myTim);
	LoadTim(&myTim, &pic[nGUICREDITS]);

	pic[nGUIGAMES].x=338;
	pic[nGUIGAMES].y=312;
	pic[nGUIGAMES].w=59;
	pic[nGUIGAMES].h=9;
	pic[nGUIGAMES].v=0;

	pic[nGUIMEMCARD].x=274;
	pic[nGUIMEMCARD].y=324;
	pic[nGUIMEMCARD].w=123;
	pic[nGUIMEMCARD].h=9;
	pic[nGUIMEMCARD].v=10;
	pic[nGUIMEMCARD].u=0;

	pic[nGUICREDITS].x=326;
	pic[nGUICREDITS].y=336;
	pic[nGUICREDITS].w=71;
	pic[nGUICREDITS].h=9;
	pic[nGUICREDITS].v=20;
	pic[nGUICREDITS].u=0;


	#if defined(DEBUG)
	printf("Done!\n");
	#endif

	/*----- This will be replaced with Skitchin's Code----*\



		printf("Loading Fonts..\n");
		GsGetTimInfo((u_long *)(FONT_TIM+4), &myTim);
	    LoadTim(&myTim, &pic[1]);
	    // Second Font tim goes here.
	  	//LoadTim(2);
			-- START





		\*----------------------------------------------------------*/


		//In The Future: (Skinning) Will use LoadImageFromCD Routine. (Kept here for future Reference)
		//LoadImageFromCD("\\FONT.TIM;1", 1, 0, 0);
		//LoadImageFromCD("\\SPLASHL.TIM;1", 10, 0, 0);
		//LoadImageFromCD("\\SPLASHR.TIM;1", 11, 0, 0);
		//LoadImageFromCD("\\MNUBL.TIM;1", 3, 0,0);
	    //LoadImageFromCD("\\MNUTL.TIM;1", 4, 0,0);
	    //LoadImageFromCD("\\MNUBR.TIM;1", 5, 0,0);
	    //LoadImageFromCD("\\MNUTR.TIM;1", 6, 0,0);
	    //LoadImageFromCD("\\GAMES_ON.TIM;1", 7, 0,0);
	    //LoadImageFromCD("\\MEMC_ON.TIM;1", 8, 0,0);
	    //LoadImageFromCD("\\CRED_ON.TIM;1", 9, 0,0);


}

void initGUI(void){
	//TODO: If this function doesnt fill up, then get rid of it.
	initImages();

}

void doSplash(void){

	int DISPLAYTIME=0;
	int DONEFLAG=0;

	#if defined(DEBUG)
			printf("Starting Splash Screen Sequence...\n");

	#endif

	while (!DONEFLAG){
		PrepScreen();
		DISPLAYTIME++;
		drawTIM(nSPLASH_L);
		drawTIM(nSPLASH_R);

		if (DISPLAYTIME >=300) {
			pic[nSPLASH_L].r=pic[nSPLASH_L].g=pic[nSPLASH_L].b--;
			pic[nSPLASH_R].r=pic[nSPLASH_R].g=pic[nSPLASH_R].b--;
			if (pic[nSPLASH_L].r==0) { DONEFLAG = 1; }
		}
		RenderWorld(0,0,0);
	}
	#if defined(DEBUG)
			printf("Splash Screen Sequence Complete...\n");
	#endif
}

void doGUIRollIn(void){
	int R,G,B;
	int DONEFLAG;
	R=G=B=0;
	DONEFLAG=0;

	#if defined(DEBUG)
			printf("Starting GUI Roll-in Sequence...\n");
	#endif

	while (!DONEFLAG){
		PrepScreen();
		if (R < 204) R=G=B+=2;
		if (R == 204) {
			if (pic[nGUIMENU_TL].y != 108) {
				pic[nGUIMENU_TL].y++;
				pic[nGUIMENU_TR].y++;
				pic[nGUIMENU_BR].y--;
				pic[nGUIMENU_BL].y--;
			} else {
				DONEFLAG = 1;
			}
		}
		drawTIM(nGUIMENU_TL);
		drawTIM(nGUIMENU_TR);
		drawTIM(nGUIMENU_BR);
		drawTIM(nGUIMENU_BL);
		RenderWorld(R,G,B);
	}

	#if defined(DEBUG)
			printf("GUI Roll-in Sequence Complete...\n");
	#endif
}

void MainMenu(){
	int NGames;
	done = 0;
	menu_pos = 0;

	#if defined(DEBUG)
			printf("Starting Main Menu...\n");
	#endif

	NGames = LoadRombank();
	while(!done) {

		PrepScreen();
		//FntPrint(games, "               Press X to Select Menu Item.\n\n");
		pad = PadRead(0);
		if (pad != lastpad) {
			if(pad & Pad1x){
				if (menu_pos == 0) {

					if (NGames){
						#if defined(DEBUG)
							printf("Number of Games Found: %d\n",NGames);
						#endif

						GameNumber = GameSelectMenu();
						if (LoadGame(GameNumber)){
							// Change_Res();
							//Clear Screen
							RenderWorld(0,0,0);
							runEmu();
						} else {
							printf("Error loading ROM.. Aborting\n");
						}
					} else {
						//Didnt find any games, or Read Error!
						printf("Error Loading Rombank.. Aborting\n");
					}
				} else if (menu_pos == 1) {
					// TODO: Add MemCard Routines here.
				} else if (menu_pos == 2) {
					// TODO: Fix Credits.
					//doCredits();
				}

			} else if(pad & Pad1Up) {
				if (menu_pos != 0)  { menu_pos --; }
			} else if (pad & Pad1Down) {
				if (menu_pos != 2) { menu_pos ++; }
			}
			lastpad = pad;
		}

		drawTIM(menu_pos + nGUIGAMES); // These should be all sequential for this to work.
		drawTIM(nGUIMENU_TL);
		drawTIM(nGUIMENU_TR);
		drawTIM(nGUIMENU_BR);
		drawTIM(nGUIMENU_BL);

		RenderWorld(0xCC, 0xCC, 0xCC);

			while ((pad & Pad1Down) || (pad & Pad1Up) || (pad & Pad1Left) || (pad & Pad1Right)) {
					pad = PadRead(0);
		}

	}
}



void doCredits(){
	//TODO: This Function needs to be redone.
	int DONEFLAG;
	int typos;
	int line;
	typos = 100;
	DONEFLAG = 0;
	while (!DONEFLAG){
		PrepScreen();

			pad = PadRead(0);
		if (pad & Pad1tri) done = 1;
		for (line = 0; line < CREDITSLINES; line++){
		    if ( (((20*line)+typos)>-100) && (((20*line)+typos)<180)) writeFont(CREDITSdata[line], 0, -160, (20 * line) + typos);
	    }
	    drawTIM(3);
		drawTIM(4);
		drawTIM(5);
		drawTIM(6);
	    if (pad & Pad1Up) typos +=3;
	    if (pad & Pad1Down) typos -=3;
	    typos -=1;
	    if (((20*CREDITSLINES)+typos)<-100) DONEFLAG = 1;

	    RenderWorld(0xCC,0xCC,0xCC);
	}
}