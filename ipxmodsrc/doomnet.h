// doomnet.h

#define PEL_WRITE_ADR   0x3c8
#define PEL_DATA        0x3c9

#define I_ColorBlack(r, g, b) { outp(PEL_WRITE_ADR, 0); outp(PEL_DATA, r); outp(PEL_DATA, g); outp(PEL_DATA, b); };

// drones aren't actually functional after Doom v1.1 so I removed
// the extra MAXPLAYERS define, one node equals one player
#define   MAXNETNODES   8

#define   CMD_SEND      1
#define   CMD_GET       2

#define   DOOMCOM_ID    0x12345678l

typedef struct {
  long id;
  short intnum;               // DOOM executes an int to send commands

  // communication between DOOM and the driver
  short command;              // CMD_SEND or CMD_GET
  short remotenode;           // dest for send, set by get (-1 = no packet)
  short datalength;           // bytes in doomdata to be sent / bytes read

  // info common to all nodes
  short numnodes;             // console is allways node 0
  short ticdup;               // 1 = no duplication, 2-5 = dup for slow nets
  short extratics;            // 1 = send a backup tic in every packet
  short deathmatch;           // 1 = deathmatch
  short savegame;             // -1 = new game, 0-5 = load savegame
  short episode;              // 1-3
  short map;                  // 1-9
  short skill;                // 1-5

  // info specific to this node
  short consoleplayer;        // 0-3 = player number
  short numplayers;           // 1-4
  
  // these don't work anymore
  short angleoffset;          // 1 = left, 0 = center, -1 = right
  short drone;                // 1 = drone

  // packet data to be sent
  char data[512];
} doomcom_t;

extern doomcom_t doomcom;
extern void interrupt (*olddoomvect) (void);
extern int vectorishooked;

extern int CheckParmWithArgs (char *c, int a);
extern int CheckParm(char *check);
extern void Error (char *error, ...);

void LaunchDOOM(void);
void interrupt NetISR(void);

