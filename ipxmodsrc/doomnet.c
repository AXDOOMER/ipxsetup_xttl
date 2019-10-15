#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <conio.h>
#include <dos.h>
#include <io.h>

#include "doomnet.h"

doomcom_t doomcom;
int vectorishooked;
void interrupt (*olddoomvect) (void);

extern int myargc;
extern char **myargv;

//
// These fields in doomcom should be filled in before calling:
// 
// short numnodes;      // console is allways node 0
// short ticdup;        // 1 = no duplication, 2-5 = dup for slow nets
// short extratics;     // 1 = send a backup tic in every packet
// short consoleplayer; // 0-3 = player number
// short numplayers;    // 1-4
// short angleoffset;   // 1 = left, 0 = center, -1 = right
// short drone;         // 1 = drone
//

const char *launchbins[6] = {"doom2.exe", "doom.exe", "hexen.exe", "heretic.exe", "strife1.exe", 0};

void LaunchDOOM (void)
{
  char *newargs[99];
  char adrstring[10];
  long flatadr;
  int i, p;

  // prepare for DOOM
  doomcom.id = DOOMCOM_ID;

  // hook the interrupt vector
  olddoomvect = _dos_getvect(doomcom.intnum);
  _dos_setvect(doomcom.intnum, (void interrupt (*)(void))MK_FP(FP_SEG(NetISR), (int)NetISR));
  vectorishooked = 1;

  // build the argument list for DOOM, adding a -net &doomcom
  memcpy(newargs, myargv, (myargc + 1) * 2);
  newargs[myargc] = "-net";
  flatadr = (long)FP_SEG(&doomcom) * 16 + (unsigned)&doomcom;
  sprintf(adrstring, "%lu", flatadr);
  newargs[myargc + 1] = adrstring;
  newargs[myargc + 2] = NULL;

  // user specified game command?
  p = CheckParmWithArgs("-exec", 1);
  if (p)
  {
    spawnv (P_WAIT, myargv[p+1], (const char **)newargs);
    printf("\nReturned from \"%s\".\n", myargv[p+1]);
    return;
  }
  // no, try to look for one in the current dir
  else
  {
    for (i=0; launchbins[i]; i++)
    {
      if (!access(launchbins[i], 0))
      {
        spawnv (P_WAIT, launchbins[i], (const char **)newargs);
        printf("\nReturned from \"%s\".\n", launchbins[i]);
	return;
      }
    }
    
    Error ("\nCouldn't find a game to launch and none specified with -exec.\n");
  }
}
