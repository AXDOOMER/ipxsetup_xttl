#include <stdio.h>
#include <string.h>
#include <process.h>
#include <i86.h>
#include <dos.h>
#include <io.h>
#include <errno.h>

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

const char *launchbins[7] = {"doom2.exe", "nr4tl.exe", "doom.exe", "hexen.exe", "heretic.exe", "strife1.exe", NULL};
#define TMPRESPFILE "IPX$$$.TMP"

void LaunchDOOM (void)
{
  FILE *respfp;
  char *newargs[99];
  char adrstring[10];
  char respparm[16];
  long flatadr;
  int i, p, r;

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
  
  printf ("Launching game...\n\n");
  
  // write args to a temporary response file to bypass process.h
  // spawnv/spawnl argument list limitations
  respfp = fopen (TMPRESPFILE, "w");
  if (!respfp)
    Error ("Cannot open response file \"%s\" for writing\n", TMPRESPFILE);
  
  for (i=1; newargs[i] != NULL; i++)
    fprintf (respfp, "%s\n", newargs[i]);
    
  fclose (respfp);
  
  sprintf (respparm, "@%s", TMPRESPFILE);
  
  // user specified game command?
  p = CheckParmWithArgs("-exec", 1);
  if (p)
  {
    r = spawnl (P_WAIT, myargv[p+1], myargv[p+1], respparm, 0);
    if (r == -1)
      Error ("Spawning \"%s\" failed (%s)", myargv[p+1],strerror(errno));
      
    printf("Returned from \"%s\".", myargv[p+1]);
    return;
  }
  // no, try to look for one in the current dir
  else
  {
    for (i=0; launchbins[i] != NULL; i++)
    {
      if (!access(launchbins[i], 0))
      {
        r = spawnl (P_WAIT, launchbins[i], launchbins[i], respparm, 0);
	if (r == -1)
	  Error ("Spawning \"%s\" failed (%s)", launchbins[i], strerror(errno));
	  
        printf("Returned from \"%s\".", launchbins[i]);
	return;
      }
    }
    
    Error ("Couldn't find a game to launch and none specified with -exec.");
  }
}
