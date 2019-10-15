#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <string.h>
#include <process.h>
#include <stdarg.h>
#include <bios.h>
#include <i86.h>
#include <graph.h>

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

void LookForNodes (void)
{
  int i, j, k;
  int netids[MAXNETNODES];
  int netplayer[MAXNETNODES];
  struct dostime_t time;
  int oldsec;
  setupdata_t *setup, *dest;
  char str[80];
  int total, console;

  // wait until we get [numnetnodes] packets, then start playing
  // the playernumbers are assigned by netid
  
  printf ("Attempting to find all players for %i player net play. "\
          "Press ESC to exit.\n", numnetnodes);
  
  printf("Looking for a node");

  oldsec = -1;
  setup = (setupdata_t*)&doomcom.data;
  mylocaltime = -1; // in setup time, not game time

  // build local setup info
  nodesetup[0].nodesfound = 1;
  nodesetup[0].nodeswanted = numnetnodes;
  doomcom.numnodes = 1;

  do
  {
    // check for aborting
    while (_bios_keybrd(1))
    {
      if ((_bios_keybrd(0) & 0xff) == 27)
        Error("\n\nNetwork game synchronization aborted.");
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
          Error("Got an unknown game packet during setup");
        
	// if it allready started, it must have found all nodes
        dest->nodesfound = dest->nodeswanted;
        continue;
      }

      // update setup ingo
      memcpy(dest, setup, sizeof(*dest));

      if (doomcom.remotenode != -1)
        continue; // allready know that node address

      // this is a new node
      memcpy(&nodeadr[doomcom.numnodes], &remoteadr,
      sizeof(nodeadr[doomcom.numnodes]));

      doomcom.numnodes++;

      printf("\nFound a node!\n");

      if (doomcom.numnodes < numnetnodes)
        printf("Looking for a node");
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

    printf("."); fflush(stdout);
    doomcom.datalength = sizeof(*setup);

    nodesetup[0].nodesfound = doomcom.numnodes;

    memcpy(&doomcom.data, &nodesetup[0], sizeof(*setup));

    SendPacket(MAXNETNODES); // send to all
  }
  while(1);

  // count players
  total = 0;
  console = 0;

  for (i = 0; i < numnetnodes; i++)
  {
    if (nodesetup[i].drone)
      continue;

    total++;

    if (total > MAXPLAYERS)
      Error("More than %i players specified!", MAXPLAYERS);
  
    if (memcmp(&nodeadr[i], &nodeadr[0], sizeof(nodeadr[0])) < 0)
      console++;
  }

  if (!total)
    Error("No players specified for game!");

  // allow overriding the automatically assigned player number from command
  // line because for some reason players will lag differently in netgames.
  i = CheckParmWithArgs("-player", 1);
  if (i)
  {
    console = atoi(myargv[i + 1]) - 1;
    if (console > total)
      console = total;
    else if (console < 0)
      console = 0;
  }
  
  doomcom.consoleplayer = console;
  doomcom.numplayers = total;

  printf("Console is player %i of %i\n", console+1, total);
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

void ShowHelp (void)
{
  printf (
    " -nodes #..........: Set number of players to # (default 2, range 1-8).\n"\
    " -dup #............: Set ticdup to # (default 1, range 1-9). Larger values will\n"\
    "                     reduce game frame rate but can help over slow links.\n"\
    " -extratics #......: Set extratics to # (default 0, range 0-8). Larger values\n"\
    "                     will use more bandwidth but can help over links that lose\n"\
    "                     packets.\n"\
    " -extratic.........: Set extratics to 1, for compatibility with source ports.\n"\
    " -player #.........: Force your player number in game to #. Can be useful\n"\
    "                     because players will lag differently in netgames. Note\n"\
    "                     that if one node is is using this parameter, then all\n"\
    "                     nodes should use it and that more than one node should not\n"\
    "                     specify the same player #. The game may hang on startup\n"\
    "                     otherwise.\n"\
    " -port #...........: Use alternate IPX network port # (default is 34460).\n"\
    " -vector 0x##......: Use interrupt vector ## (in hexadecimal). If not specified\n"\
    "                     will try to find a free one between 0x60 and 0x66.\n"\
    " -exec cmd.........: Try to run \"cmd\" instead of looking for, in this order,\n"\
    "                     \"doom2.exe\", \"doom.exe\", \"hexen.exe\", \"heretic.exe\",\n"\
    "                     \"strife1.exe\".\n"\
  );
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
  
  _clearscreen(_GCLEARSCREEN);
  printf ("IPX network driver for Doom, Heretic, Hexen and Strife (with modifications)\n"\
          "This version compiled on %s / %s by xttl @ doomworld.com/vb/\n"\
	  "\n", __DATE__, __TIME__);

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
    if (doomcom.ticdup > 9)
      doomcom.ticdup = 9;
    else if (doomcom.ticdup < 1)
      doomcom.ticdup = 1;
  }
  else
    doomcom.ticdup = 1;

  p = CheckParmWithArgs("-extratics", 1);
  if (p)
  {
    doomcom.extratics = atoi (myargv[p+1]);
    if (doomcom.extratics > 8)
      doomcom.extratics = 8;
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
    if (numnetnodes > MAXPLAYERS)
      numnetnodes = MAXPLAYERS;
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
