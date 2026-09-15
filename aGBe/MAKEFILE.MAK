

ASM             = asmpsx
CC              = ccpsx
LINK            = psylink

CCOPTS          = -Wunused -comments-c++ -c -O3
LINKOPTS        = /c /z /m

OBJS            =  main.obj   \
		   gui.obj    \
		   emu.obj    \
		   opcodes.obj \
		   psx.obj

#Make & Link


all: main.cpe
        

main.cpe: $(OBJS) main.ln makefile.mak
          $(LINK) $(LINKOPTS) @main.ln,main.cpe,main.sym,main.map
	  @cpe2exe /CJ main.cpe
	  @del main.cpe

main.obj: main.c main.h
        $(CC) $(CCOPTS) main.c -o main.obj
        #DMPSX -b main.obj

psx.obj: psx.c psx.h
        $(CC) $(CCOPTS) psx.c -o psx.obj

gui.obj: gui.c gui.h
        $(CC) $(CCOPTS) gui.c -o gui.obj
       
emu.obj: emu.c emu.h
        $(CC) $(CCOPTS) emu.c -o emu.obj
        
opcodes.obj: opcodes.c opcodes.h
	$(CC) $(CCOPTS) opcodes.c -o opcodes.obj
       
#player.obj: player.c player.h
#        $(CC) $(CCOPTS) player.c -o player.obj
#        #DMPSX -b player.obj


#sound.obj: sound.c
#        $(CC) $(CCOPTS) sound.c -o sound.obj

#a.obj: a.s
#       $(ASM) /l /c a.s,a.obj


clean: 
        del *.obj    
	del *.cpe
	del *.sym
	del *.map
	del *.bak


                                                                        