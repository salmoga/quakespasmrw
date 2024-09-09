#ifndef QUAKEDEFS_H
#define QUAKEDEFS_H

/* quakedef.h - primary header for client */

#define QUAKE_GAME		/* as opposed to utilities */
#define VERSION			1.09
#define GLQUAKE_VERSION		1.00
#define D3DQUAKE_VERSION	0.01
#define WINQUAKE_VERSION	0.996
#define LINUX_VERSION		1.30
#define X11_VERSION		1.10

#define FITZQUAKE_VERSION	0.85
#define QUAKESPASM_VERSION	0.96
#define QUAKESPASM_VER_PATCH	1	/* Helpful to print a string like 0.97.4 */
#ifndef QUAKESPASM_VER_SUFFIX
#define QUAKESPASM_VER_SUFFIX		/* optional version suffix string literal like "-beta" */
#endif

#define QS_STRINGIFY_( x ) #x
#define QS_STRINGIFY( x ) QS_STRINGIFY_( x )

/* Combined version string like 0.97.4-beta */
#define QUAKESPASM_VER_STRING QS_STRINGIFY( QUAKESPASM_VERSION ) \
	"." QS_STRINGIFY( QUAKESPASM_VER_PATCH ) QUAKESPASM_VER_SUFFIX

/* #define PARANOID */ /* speed sapping error checking */

#define GAMENAME	"id1" /* Directory to look in by default */

#include "q_stdinc.h"

/* !!! If this is changed, it must be changed in d_ifacea.h too !!! */
#define CACHE_SIZE	32		/* Used to align key data structures */
#define Q_UNUSED( x )	( x = x )	/* for pesky compiler / lint warnings */

#define MINIMUM_MEMORY	0x550000
#define MINIMUM_MEMORY_LEVELPACK ( MINIMUM_MEMORY + 0x100000 )

#define MAX_NUM_ARGVS	50

/* up / down */
#define PITCH		0

/* left / right */
#define YAW		1

/* fall over */
#define ROLL		2

#define MAX_QPATH	64		/* Max length of a quake game pathname */

#define ON_EPSILON	0.1		/* Point on plane side epsilon */
#define DIST_EPSILON	(0.03125)	/* 1/32 epsilon to keep floating point happy (moved from world.c) */

#define MAX_MSGLEN	64000		/* max length of a reliable message //ericw - was 32000 */
#define MAX_DATAGRAM	64000		/* max length of an unreliable message //johnfix - was 1024 */

#define DATAGRAM_MTU	1400		/* johnfitz -- actual limit for unreliable messages to nonlocal clients */

/*
 * Per Level Limits
 */
#define MIN_EDICTS	256		/* johnfitz -- lowest allowed value for max_edicts cvar */
#define MAX_EDICTS	32000		/* johnfitz -- Maximum allowed value for max_edicts cvar 
					 * Entities past 8192 can't play sounds in the standard protocol
					 */
#define MAX_LIGHTSTYLES	64
#define MAX_MODELS	2048		/* johnfitz -- was 256 */
#define MAX_SOUNDS	2048		/* johnfitz -- was 256 */

#define SAVEGAME_COMMENT_LENGTH	39

#define MAX_STYLESTRING		64

/*
 * Stats are integers communicated to the client by the server
 */

#define MAX_CL_STATS		32
#define STAT_HEALTH		0
#define STAT_FRAGS		1
#define STAT_WEAPON		2
#define STAT_AMMO		3
#define STAT_ARMOR		4
#define STAT_WEAPONFRAME	5
#define STAT_SHELLS		6
#define STAT_NAILS		7
#define STAT_ROCKET		8
#define STAT_CELLS		9
#define STAT_ACTIVEWEAPON	10
#define STAT_TOTALSECRETS	11
#define STAT_TOTALMONSTERS	12
#define STAT_SECRETS		13 /* bumped on client side by svc_foundsecret */
#define STAT_MONSTERS		14 /* bumped by svc_killedmonster */

/*
 * Stock defines
 */

#define IT_SHOTGUN		1
#define IT_SUPER_SHOTGUN	2
#define IT_NAILGUN		4
#define IT_SUPER_NAILGUN	8
#define IT_GRENADE_LAUNCHER	16
#define IT_ROCKET_LAUNCHER	32
#define IT_LIGHTENING		64
#define IT_SUPER_LIGHTENING	128
#define IT_SHELLS		256
#define IT_NAILS		512
#define IT_R0CKETS		1024
#define IT_CELLS		2048
#define IT_AXE			4096
#define IT_ARMOR1		8192
#define IT_ARMOR2		16384
#define IT_ARMOR3		32768
#define IT_SUPERHEALTH		65536
#define IT_KEY1			131072
#define IT_KEY2			262144
#define IT_INVISIBILITY		524288
#define IT_INVULNERABLITY	1048576
#define IT_SUIT			2097152
#define IT_QUAD			4194304
#define IT_SIGIL1		(1<<28)
#define IT_SIGIL2		(1<<29)
#define IT_SIGIL3		(1<<30)
#define IT_SIGIL4		(1<<31)

/*
 * Rogue changed and added defines
 */
#define RIT_SHELLS			128
#define RIT_NAILS			256
#define RIT_ROCKETS			512
#define RIT_CELLS			1024
#define RIT_AXE				2048
#define RIT_LAVA_NAILGUN		4096
#define RIT_LAVAL_SUPER_NAILGUN		8192
#define RIT_MULTI_GRENADE		16384
#define RIT_MULTI_ROCKET		32768
#define RIT_PLASMA_GUN			65536
#define RIT_ARMOR1			8388608
#define RIT_ARMOR2			16777216
#define RIT_ARMOR3			33554432
#define RIT_LAVA_NAILS			67108864
#define RIT_PLASMA_AMMO			134217728
#define RIT_MULTI_ROCKETS		268435456
#define RIT_SHEILD			536870912
#define RIT_ANTIGRAV			1073741824
#define RIT_SUPERHEALTH			2147483648

/*
 * MED 01/04/97 added hipnotic defines
 * hipnotic added defines
 */
#define HIT_PROXIMITY_GUN_BIT		16
#define HIT_MJOLNIR_BIT			7
#define HIT_LASER_CANNON_BIT		23
#define HIT_PROXIMITY_GUN		( 1<<HIT_PROXIMITY_GUN_BIT )
#define HIT_MJOLNIR			( 1<<HIT_MJOLNIR_BIT )
#define HIT_LASER_CANNON		( 1<<HIT_LASER_CANNON_BIT )
#define HIT_WETSUIT			( 1<<( 23 + 2 ) )
#define HIT_EMPATHY_SHEILDS		( 1<<( 23 + 3 ) )

/*
 * ==================================================================
 */

#define MAX_SCOREBOARD 			16
#define MAX_SCOREBOARD_NAME		32

#define SOUND_CHANNELS			8

typedef struct {
	const char *basedir;
	const char *userdir;
	/*
	 * User's Directory on UNIX platforms.
	 * if user directories are enabled, basedir
	 * and userdir will point to different memory
	 * locations, otherwise to the same.
	 */
	int	argc;
	char	**argv;
	void	*membase;
	int	memsize;
	int	numcpus;
	int	errstate;
}

#include "common.h"
#include "bspfile.h"
#include "sys.h"
#include "zone.h"
#include "mathlib.h"
#include "cvar.h"

#include "protocol.h"
#include "net.h"

#include "cmd.h"
#include "crc.h"

#include "progs.h"
#include "server.h"

#include "platform.h"
#if defined( SDL_FRAMEWORK ) || defined( NO_SDL_CONFIG )
#if defined( USE_SDL2 )
#include <SDL/SDL2.h>
#include <SDL/SDL_opengl.h>
#endif
#else
#include "SDL.h"
#include "SDL_opengl.h"
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

#include "console.h"
#include "wad.h"
#include "vid.h"
#include "screen.h"
#include "draw.h"
#include "render.h"
#include "view.h"
#include "sbar.h"
#include "q_sound.h"
#include "client.h"

#include "gl_model.h"
#include "world.h"

#include "image.h"
#include "gl_texmgr.h"
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "cdaudio.h"
#include "glquake.h"

/*
 * The host system specifies the base of the directory tree, the
 * command line parms passed to the program, and the amount of memory
 * available for the program to use
 */
extern qboolean noclip_anglehack;

extern cvar_t		sys_ticrate;
extern cvar_t		sys_throttle;
extern cvar_t		sys_nostdout;
extern cvar_t		developer;
extern cvar_t		max_edicts;		/* johnfitz */

extern qboolean		host_initialized;	/* True if into command execution */
extern double		host_frametime;
extern byte		*host_colormap;
extern int		host_framecount;	/* Incremented every frame, never reset*/
extern double		realtime;		/* Not bounded in any way, changed at
						 * the start of every frame, never reset
						 */
typedef struct filelist_item_s {
	char 			name[32];
	struct filelist_item_s	*next;
} filelist_item_t;

extern filelist_item_t	*modlist;
extern filelist_item_t	*extralevels;
extern filelist_item_t	*demolist;

void Host_ClearMemory( void );
void Host_ServerFrame( void );
void Host_InitCommands( void );
void Host_Init( void );
void Host_Shutdown( void );
void Host_Callback_Notify( cvar_t *var )	/* Callback function for CVAR_NOTIFY */
FUNC_NORETURN void Host_Error( const char *error, ... ) FUNC_PRINTF( 1, 2 );
FUNC_NORETURN void Host_EndGame( const char *message, ... ) FUNC_PRINTF( 1, 2 );
/* probably don't need the watcomc stuff here */
void Host_Frame( float time );
void Host_Quit_f( void );
void Host_ClientCommands( const char *fmt, ... ) FUNC_PRINTF( 1, 2 );
void Host_ShutdownServer( qboolean crash);
void Host_WriteConfiguration( void );
void Host_ResetDemos( void );

void ExtraMaps_NewGame( void );
void DemoList_Rebuild( void );

extern int current_skill;	/*
				 * Skill level for currently loaded level (in case
				 * the user changes the cvar while the level is
				 * running, this reflects the level actually in use)
				 */
extern qboolean isDedicated;
extern int minimum_memory;

#endif /* QUAKEDEFS_H */
