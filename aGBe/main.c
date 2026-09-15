/* --------------------------------------------------


             aGBe - a game boy emulator
             		for the psx

    	           	(RELOADED)


  	VERSION '0.2.1' - Fixed and tested CD Loading/Reading routines.
	  					  Rearranged CPU Opcode instructions.
  					  Added agbeBank loading routines.
  	VERSION '0.2.0' - Revival. Updating and getting rid of code. Massive clean-up.


   -------------------------------------------------- */


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
#include "pad.h"
#include "main.h"
#include "psx.h"
//#include "mem.h"
#include "gui.h"




// functions ////////////////////////////////////////////////////
int main(void){
	#if defined(DEBUG)
	printf("aGBe \n");
	#endif


	init_PSX();

	#if defined(DEBUG)
	printf("Initializing CD...\n");
	#endif

	CdInit();

	#if defined(DEBUG)
		printf("Initializing CD...Complete\n");
	#endif
    //initFont();
    //loadFonts(1);

	#if defined(DEBUG)
		printf("Initializing GUI...\n");
	#endif
	initGUI();
	#if defined(DEBUG)
		printf("Initializing GUI...Complete\n");
	#endif

	while(1) {
		#if defined(RELEASE)
		doSplash();
		doGUIRollIn();
		#endif
		MainMenu();
	}
return 0;
}




