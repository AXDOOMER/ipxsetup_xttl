#include <stdio.h>
#include <stdlib.h>
#include <i86.h>
#include <dos.h>
#include <string.h>
#include <stdarg.h>
#include <bios.h>

#include "ipxnet.h"

int gameid;
int numnetnodes;
long socketid;
int myargc;
char **myargv;

setupdata_t nodesetup[MAXNETNODES];

//
// For abnormal program terminations
//

void Error (char *error, ...)
{
  va_list argptr;

  if (vectorishooked)
    _dos_setvect(doomcom.intnum, olddoomvect);

  printf("ERROR: ");
  va_start(argptr, error);
  vprintf(error, argptr);
  va_end(argptr);
  printf("\n");
  
  ShutdownNetwork();
  
  exit(1);
}

//
// Checks for the given parameter in the program's command line arguments
//
// Returns the argument number (1 to argc-1) or 0 if not present
//

int CheckParmWithArgs (char *parm, int num_args)
{
  int i;

  for (i = 1; i < myargc-num_args; i++)
  {
    if (stricmp(parm, myargv[i]) == 0)
      return i;
  }

  return 0;
}

int CheckParm (char *parm)
{
  return CheckParmWithArgs(parm,0);
}


void interrupt NetISR (void)
{
  if (doomcom.command == CMD_SEND)
  {
    mylocaltime++;
    SendPacket(doomcom.remotenode);
  }
  else if (doomcom.command == CMD_GET)
  {
    GetPacket();
  }
}

//
// Finds all the nodes for the game and works out player numbers among them
// Exits with nodesetup[0..numnodes] and nodeadr[0..numnodes] filled in
//

static const char waitchars[] = "|/-\\";

void LookForNodes (void)
{
  int i, j, k;
  int netids[MAXNETNODES];
  int netplayer[MAXNETNODES];
  struct dostime_t time;
  int oldsec;
  setupdata_t *setup, *dest;
  char str[80];
  // int console;

  // wait until we get [numnetnodes] packets, then start playing
  // the playernumbers are assigned by netid
  
  printf ("Attempting to find all players for %i player net play. "\
          "Press ESC to exit.\n\n", numnetnodes);
  
  printf("Looking for a node... ");

  oldsec = -1;
  setup = (setupdata_t*)&doomcom.data;
  mylocaltime = -1; // in setup time, not game time

  // build local setup info
  nodesetup[0].nodesfound = 1;
  nodesetup[0].nodeswanted = numnetnodes;
  doomcom.numnodes = 1;
  nodesetup[0].dupwanted = doomcom.ticdup;

  // allow overriding the automatically assigned player number from command
  // line because for some reason players will lag differently in netgames.
  i = CheckParmWithArgs("-player", 1);
  if (i)
  {
    j = atoi(myargv[i+1]);
    if (j > numnetnodes)
      j = numnetnodes;
    else if (j < 1)
      j = 1;
  }
  // use auto-assign
  else
    j = -1;
  
  nodesetup[0].plnumwanted = j;

  do
  {
    printf("%c\b", waitchars[time.second%4]); fflush(stdout);

    // check for aborting
    while (_bios_keybrd(1))
    {
      if ((_bios_keybrd(0) & 0xff) == 27)
        Error("\rNetwork game synchronization aborted.");
    }
    
    // listen to the network
    while (GetPacket())
    {
      if (doomcom.remotenode == -1)
        dest = &nodesetup[doomcom.numnodes];
      else
        dest = &nodesetup[doomcom.remotenode];

      if (myremotetime != -1)
      {
        // an early game packet, not a setup packet
        if (doomcom.remotenode == -1)
          Error("\rGot an unknown game packet during setup");
        
	// if it allready started, it must have found all nodes
        dest->nodesfound = dest->nodeswanted;
        continue;
      }

      // update setup ingo
      memcpy(dest, setup, sizeof(*dest));

      if (doomcom.remotenode != -1)
        continue; // allready know that node address

      // this is a new node
      memcpy(&nodeadr[doomcom.numnodes], &remoteadr, sizeof(nodeadr[doomcom.numnodes]));
      
      doomcom.numnodes++;

      printf("found a node!\n");

      if (doomcom.numnodes < numnetnodes)
        printf("Looking for a node... ");
    }

    // we are done if all nodes have found all other nodes
    for (i = 0; i < doomcom.numnodes; i++)
    {
      if (nodesetup[i].nodesfound != nodesetup[i].nodeswanted)
        break;
    }

    if (i == nodesetup[0].nodeswanted)
      break; // got them all

    // send out a broadcast packet every second
    _dos_gettime(&time);
    if (time.second == oldsec)
      continue;
    oldsec = time.second;
   
    doomcom.datalength = sizeof(*setup);

    nodesetup[0].nodesfound = doomcom.numnodes;

    memcpy(&doomcom.data, &nodesetup[0], sizeof(*setup));

    SendPacket(MAXNETNODES); // send to all
  }
  while(1);
  
  if (numnetnodes == 1)
    printf ("\r");
  
  // Check that everyone is using the same ticdup setting (game will not work
  // otherwise). Check for duplicate player numbers. (game will hang on startup)
  // and that everyone is either using automatic or manually assigned player
  // numbers (so we don't have one node requesting the same player number
  // manually that got automatically assigned to another and the game hanging).
  
  // check ticdup settings
  for (i=0; i<numnetnodes; i++)
  {
    for (j=0; j<numnetnodes; j++)
    {
      if (nodesetup[j].dupwanted != nodesetup[i].dupwanted)
        Error ("All nodes must use the same ticdup setting!");
    }
  }
  
  // auto-assign player numbers based on IPX network address?
  if (nodesetup[0].plnumwanted == -1)
  {
    // check that everyone is using automatic assignment
    for (i=1; i<numnetnodes; i++)
    {
      if (nodesetup[i].plnumwanted != -1)
        Error ("Every node must set player numbers manually or use automatic assignment!");
    }
    
    doomcom.consoleplayer = 0;
    
    for (i=0; i<numnetnodes; i++)
    {
      if (memcmp (&nodeadr[i], &nodeadr[0], sizeof(nodeadr[0])) < 0)
        doomcom.consoleplayer++;
    }
  }
  // use manual assignment
  else
  {
    // check that everyone is using manual assignment and that there are
    // no duplicate player numbers
    for (i=0; i<numnetnodes; i++)
    {
      if (nodesetup[i].plnumwanted == -1)
        Error ("Every node must set player numbers manually or use automatic assignment!");

      for (j=0; j<numnetnodes; j++)
      {
        if (nodesetup[j].plnumwanted == nodesetup[i].plnumwanted && j!=i)
          Error ("More than one node requested the same player number!");
      }
    }
    
    doomcom.consoleplayer = nodesetup[0].plnumwanted-1;
  }
   
  doomcom.numplayers = numnetnodes;
  
  printf("Console is player %i of %i (%s assignment).\n", doomcom.consoleplayer+1, numnetnodes,
   nodesetup[0].plnumwanted==-1 ? "automatic" : "manual");
  printf("TicDup is %i, ExtraTics is %i.\n", doomcom.ticdup, doomcom.extratics);
}

void FindResponseFile (void)
{
  int i;

  #define MAXARGVS 100

  for (i = 1; i < myargc; i++)
  {
    if (myargv[i][0] == '@')
    {
      FILE *handle;
      int size;
      int k;
      int index;
      int indexinfile;
      char *infile;
      char *file;
      char *moreargs[20];
      char *firstargv;

      // READ THE RESPONSE FILE INTO MEMORY
      handle = fopen(&myargv[i][1], "rb");
      
      if (!handle)
        Error ("No such response file!");
      
      printf ("Found response file \"%s\"!\n", strupr(&myargv[i][1]));
      
      fseek(handle, 0, SEEK_END);
      size = ftell(handle);
      fseek(handle, 0, SEEK_SET);
      file = malloc(size);
      fread(file, size, 1, handle);
      fclose(handle);

      // KEEP ALL CMDLINE ARGS FOLLOWING @RESPONSEFILE ARG
      for (index = 0, k = i + 1; k < myargc; k++)
        moreargs[index++] = myargv[k];

      firstargv = myargv[0];
      myargv = malloc(sizeof(char *) * MAXARGVS);
      memset(myargv, 0, sizeof(char *) * MAXARGVS);
      myargv[0] = firstargv;

      infile = file;
      indexinfile = k = 0;
      indexinfile++;                  // SKIP PAST ARGV[0] (KEEP IT)

      do
      {
        myargv[indexinfile++] = infile + k;
        
	while (k < size && ((*(infile + k) >= ' ' + 1) && (*(infile + k) <= 'z')))
          k++;
        
	*(infile + k) = 0;
        
	while (k < size && ((*(infile + k) <= ' ') || (*(infile + k) > 'z')))
          k++;
      }
      while (k < size);

      for (k = 0; k < index; k++)
        myargv[indexinfile++] = moreargs[k];
   
      myargc = indexinfile;

      break;
    }
  }
}

static void ShowHelp (void)
{
  printf (
    " -nodes #..........: Set number of players to # (default 2, range is 1-8 but\n"\
    "                     only use more than 4 in Hexen v1.1 and Strife)\n"\
    " -player #.........: Set player number to # in game instead of automatically\n"\
    "                     assigning one based on IPX network address. Can be useful\n"\
    "                     because different players will not experience lag the\n"\
    "                     same way and which player is best depends on link latency.\n"\
    "                     If any node uses this option then all nodes must use it.\n"\
    " -dup #............: Set ticdup to # (default 1, range 1-5). Larger values will\n"\
    "                     reduce game frame rate but can help over slow links. In\n"\
    "                     Strife this setting is ignored, engine sets ticdup to 2.\n"\
    " -extratics #......: Set extratics to # (default 0, range 0-7). Larger values\n"\
    "                     will use more bandwidth but can help over links that lose\n"\
    "                     packets.\n"\
    " -extratic.........: Set extratics to 1, for compatibility with source ports.\n"\
    " -port #...........: Use alternate IPX network port # (default is 34460).\n"\
    " -vector 0x##......: Use interrupt vector ## (in hexadecimal). If not specified\n"\
    "                     will try to find a free one between 0x60 and 0x66.\n"\
    " -exec cmd.........: Try to run \"cmd\" instead of looking for, in this order,\n"\
    "                     \"doom2.exe\", \"nr4tl.exe\", \"doom.exe\", \"hexen.exe\",\n"\
    "                     \"heretic.exe\", \"strife1.exe\".\n"\
  );
}

// I tried using Watcom's graph.h functions here, but they bloat the executable
// by over 22kB... though I wonder if anyone is really going to use this on
// real hardware and is using a configuration that is so conventional memory
// starved...

// every line should be 80 chars long and without explicit newline/CR
static const char *titlelines = "  IPX network driver for Doom, Heretic, Hexen and Strife (with modifications)   "\
                                "  This version compiled on " __DATE__ " / " __TIME__ " by xttl @ doomworld.com/vb/   ";

static void ShowTitle (void)
{
  union REGPACK regs;
  
  // set 80x25 16 color text mode using video bios, will also clear screen
  // and reset cursor if we were already in this mode
  regs.x.ax = 0x0002;  // AH=0x00 (set mode), AL=0x02 (mode 2 = 80x25 text with 16 colors)
  intr (0x10, &regs);
  
  // print colored string using video bios
  regs.x.ax = 0x1301;  // AH=0x13 (write string) AL=0x01 (write mode bit1 = update cursor)
  regs.x.bx = 0x001a;  // BH=0x00 (vidpage 0) BL=0x1A (attribute: bright green A on blue 1)
  regs.x.cx = strlen(titlelines);
  regs.x.dx = 0;       // DH,DL = start row & column
  regs.x.es = FP_SEG (titlelines);
  regs.x.bp = FP_OFF (titlelines); // ES:BP = address of string to write
  intr (0x10, &regs);
  
  printf ("\n");
}

int main (int argc, char **argv)
{
  unsigned char far *vector;
  int p;

  // determine game parameters
  gameid = 0;
  numnetnodes = 2;
  // doomcom.ticdup = 1;
  // doomcom.extratics = 1;
  doomcom.episode = 1;
  doomcom.map = 1;
  doomcom.skill = 2;
  doomcom.deathmatch = 0;
  // these actually don't do anything anymore after Doom v1.1
  doomcom.drone = doomcom.angleoffset = 0;
  
  ShowTitle();

  myargc = argc;
  myargv = argv;
  FindResponseFile();
  
  if (CheckParm("-?") || CheckParm("-h") || CheckParm("-help"))
  {
    ShowHelp();
    return 0;
  }

  p = CheckParmWithArgs("-dup", 1);
  if (p)
  {
    doomcom.ticdup = atoi (myargv[p+1]);
    if (doomcom.ticdup > 5)
      doomcom.ticdup = 5;
    else if (doomcom.ticdup < 1)
      doomcom.ticdup = 1;
  }
  else
    doomcom.ticdup = 1;

  p = CheckParmWithArgs("-extratics", 1);
  if (p)
  {
    doomcom.extratics = atoi (myargv[p+1]);
    if (doomcom.extratics > 100)
      doomcom.extratics = 100;
    else if (doomcom.extratics < 0)
      doomcom.extratics = 0;
  }
  else if (CheckParm("-extratic"))
    doomcom.extratics = 1;
  else
    doomcom.extratics = 0;

  p = CheckParmWithArgs("-nodes", 1);
  if (p)
  {
    numnetnodes = atoi(myargv[p+1]);
    if (numnetnodes > MAXNETNODES)
      numnetnodes = MAXNETNODES;
    else if (numnetnodes < 1)
      numnetnodes = 1;
  }
  else
    numnetnodes = 2;

  p = CheckParmWithArgs("-vector", 1);
  if (p)
  {
    doomcom.intnum = sscanf("0x%x", myargv[p+1]);
    vector = *(char far * far *)(doomcom.intnum * 4);

    if (vector != NULL && *vector != 0xcf)
      Error ("The specified vector (0x%02x) was already hooked.\n", doomcom.intnum);
  }
  else
  {
    for (doomcom.intnum = 0x60; doomcom.intnum <= 0x66; doomcom.intnum++)
    {
      vector = *(char far * far *)(doomcom.intnum * 4);
      if (vector == NULL || *vector == 0xcf)
        break;
    }

    if (doomcom.intnum == 0x67)
    {
      Error ("Warning: no NULL or iret interrupt vectors were found in the 0x60 to 0x66\n" \
              "range.  You can specify a vector with the -vector 0x<num> parameter.\n");
    }
  }
  
  printf ("Communicating with interrupt vector 0x%x\n", doomcom.intnum);
  
  p = CheckParmWithArgs("-port", 1);
  if (p)
  {
    socketid = atol(myargv[p+1]);
    printf ("Using alternate port %i for network\n", socketid);
  }
  else
    socketid = 0x869C; // 0x869c is the official DOOM socket

  InitNetwork();

  LookForNodes();

  mylocaltime = 0;

  LaunchDOOM();

  ShutdownNetwork();

  if (vectorishooked)
    _dos_setvect(doomcom.intnum, olddoomvect);

  return 0;
}
