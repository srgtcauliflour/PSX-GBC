void init_PSX(void);
void PrepScreen();
void initFont(void);
void loadFonts(GsIMAGE *im, int fntNum);
void writeFont(char *ch, int FntNumba, int x, int y);
void drawTIM(int timNumber);
void RenderWorld(BYTE re, BYTE gr, BYTE bl);
void LoadTim(GsIMAGE *im, GsSPRITE *sp);
void Change_Res(void);
void Draw_Buffer(int *screenBuffer);
void Draw_Buffer_SPRT(int *screenBuffer);
void Draw_Buffer_Pixel_Blitting(int *screenBuffer);
void LoadFiles(void);
int LoadRombank(void);
void LoadImageFromCD(char *name, int N, int X, int Y);
int LoadGame(int GameNumber);
void DispInfo(int GameNumber);
int GameSelectMenu(void);

struct RomList{
	char	name[28];
	DWORD 	size;
	DWORD	loc;
};

struct RomHeader{
	char	nothing[308];
	char	title[16];
	char	newlic[2];
	char	sgb;
	char	cart;
	char	romsize;
	char	sramsize;
	char	country;
	char	oldlic;
	char	nothing2[1716];

};

typedef struct {
	GsIMAGE Image;
	int u,v;
	int w,h;
	char *order;
	int timNum;
	GsSPRITE fnttxt;
} Font;


typedef struct {
    int       visible;
	GsDOBJ2		handler;
	VECTOR		position;
	SVECTOR		rotate;
	GsCOORDINATE2	coord;
} ObjectHandler;

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define PACKETMAX  	  300
#define OT_LENGTH	   5
#define MAX_TEXTURES   20


GsOT myOT[2];
GsOT_TAG myOT_TAG[2][1<<OT_LENGTH];
PACKET GpuPacketArea[2][PACKETMAX*24];
GsSPRITE pic[MAX_TEXTURES];
GsIMAGE myTim;

RECT myRect;
Font myFont[2];

TILE pix;
SPRT sprtPIXEL;

int	gnumber;
CdlFILE	cdf;

char timbuffer[1];
//char timbuffer[155648];		// ~150k needs unalloc
long	games;				// Fnt screen
// BUG FIX: was a real (tentative) definition duplicated across this header
// AND pad.h (both included together by main.c/psx.c/gui.c/emu.c) - two
// separate tentative definitions of the same symbols even within one
// file, before considering every other file that includes both headers.
// `extern` here; the one real definition lives in psx.c.
extern u_long pad, lastpad;
int activeBuffer;

struct RomList list[1024];		//128k
//char	rombuffer[32768];	//128k
//char ROM[131072];
//char ROM[131072];
// BUG FIX: was a real (tentative) definition here; extern now, real
// definition lives in psx.c (see the note there).
extern BYTE *ROM;
u_long  filePos;