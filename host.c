/* host.c - Coordinates spawning and killing of local servers */

#include "quakedef.h"
#include "bgmusic.h"
#include <setjmp.h>

/*
 * A server can always be started, even if the system started out
 * as a client to a remote system.
 * A client can NOT be started if the system started as a dedicated
 * server.
 * Memory is cleared / released when a server or client begins, not when they end.
 */
quakeparms_t *host_parms;

qboolean	host_initialized

double		host_frametime;
double		realtime;
double		oldrealtime;

int		host_framecount;

int		host_hunklevel;

int		minimum_memory;

client_t	*host_client;

jmp_buf		host_abortserver;

byte		*host_colormap;

/* emes - I think CVAR_ARCHIVE is written automatically when config.cfg is written on exit */
/* TODO: make an option where it doesn't modify anything at all, perhaps -nomodify */


cvar_t host_framerate = { "host_framerate", "0", CVAR_NONE };	/* Set for slowmotion */
cvar_t host_speeds = { "host_speeds", "0", CVAR_NONE };	/* Set for running times */
cvar_t host_maxfps = { "host_maxfps", "0", CVAR_ARCHIVE };	/* johnfitz */
cvar_t host_timescale = { "host_timescale", "0", CVAR_NONE };	/* johnfitz */
cvar_t max_edicts = { "max_edicts", "8192", CVAR_NONE };	/* johnfitz // ericw - changed from 2048 to
									 * 8192, removed CVAR_ARCHIVE */
cvar_t sys_ticrate = { "sys_ticrate", "0.05", CVAR_NONE  };	/* dedicated server */
cvar_t serverprofile = { "serverprofile", "0", CVAR_NONE };

cvar_t fraglimit = { "fraglimit", "0", CVAR_NOTIFY|CVAR_SERVERINFO };
cvar_t timelimit = { "timelimit", "0", CVAR_NOTIFY|CVAR_SERVERINFO };
cvar_t teamplay = { "teamplay", "0", CVAR_NOTIFY|CVAR_SERVERINFO };
cvar_t samelevel = { "samelevel", "0", CVAR_NONE };
cvar_t noexit = { "noexit", "0", CVAR_NOTIFY|CVAR_SERVERINFO };
cvar_t skill = { "skill", "1", CVAR_NONE };			/* 0 - 3 */
cvar_t deathmatch = { "deathmatch", "0", CVAR_NONE };
cvar_t coop = { "coop", "0", CVAR_NONE };

cvar_t pausable = { "pausable", "1", CVAR_NONE };

cvar_t developer = { "developer", "0", CVAR_NONE };

cvar_t temp1 = { "temp1", "0", CVAR_NONE };

cvar_t devstats = { "devstats", "0", CVAR_NONE }; /* johnfitz -- track developer statistics that vary every frame */

/*
 * For the 2021 rerelease
 */
cvar_t campaign = { "campaign", "0", CVAR_NONE };
cvar_t horde = { "horde", "0", CVAR_NONE };
cvar_t sv_cheats = { "sv_cheats", "0", CVAR_NONE };

devstats_t dev_stats, dev_peakstats;
/*
 * This stores the last time overflow messages were displayed, not the last time overflows occured
 */
overflowtimes_t dev_overflows;

/*
===============
Max_Edicts_f -- johnfitz
===============
*/
static void Max_Edicts_f( cvar_t *var ) {
	/* TODO: Clamp it here? */
	if ( cls.state == ca_connected || sv_active ) {
		Con_Printf( "Changes to max_edicts will not take effect until next time a map is loaded.\n" );
	}
}

/*
===============
Max_Fps_f -- ericw
===============
*/
static void Max_Fps_f( cvar_t *var ) {
	if ( var->value > 72 ) {
		Con_Warning( "host_maxfps above 72 breaks physics.\n" );
	}
}

/*
===============
Host_EndGame
===============
*/
void Host_EndGame( const char *message, ... ) {
	va_list		argptr;
	char		string[1024];

	va_start( argptr, message );
	q_vsnprintf( string, sizeof( string ), message, argptr );
	va_end( argptr );
	Con_DPrintf( "Host_EndGame: %s\n", string );

	if ( sv.active ) {
		Host_ShutdownServer( false );
	}

	if ( cls.state == ca_dedicated ) {
		Sys_Error( "Host_EndGame: %s\n", string );
	}
	if ( cls.demonum != -1 && !cls.timedemo ) {
		CL_NextDemo( );
	}
	else {
		CL_Disconnect( );
	}
	longjmp( host_abortserver, 1 );
}

/*
===============
Host_Error

This shuts down both the client and the server
===============
*/
void Host_Error( const char *error, ... ) {
	va_list		argptr;
	char		string[1024];
	static qboolean	inerror = false;

	if ( inerror ) {
		Sys_Error( "Host_Error: recursively entered" );
	}

	inerror = true;

	SCR_EndLoadingPlaque( ); /* Re-enable screen updates */

	va_start( argptr, error );
	q_vsnprintf( string, sizeof( string ), error, argptr );
	va_end( argptr );
	ConPrintf( "Host_Error: %s\n", string );

	if ( sv.active ) {
		Host_ShutdownServer( false );
	}
	if ( cls.state == ca_dedicated ) {
		Sys_Error ( "Host_Error: %s\n", string ); /* Dedicated servers exit */
	}

	CL_Disconnect( );
	cls.demonum = -1;
	cl.intermission = 0;

	inerror = false;

	longjmp( host_abortserver, 1 );
}

/*
===============
Host_FindMaxClients
===============
*/
void Host_FindMaxClients(void) {
	integer	ii;
	svs.maxclients = 1;

	ii = COM_CheckParm("-dedicated");
	if ( ii ) {
		cls.state = ca_dedicated;
		if ( ii != ( com_argc -1 ) ) {
			svs.maxclients = Q_atoi( com_argv[ ii + 1 ]);
		}
		else {
			svs.maxclients = 8;
		}
	}
	else {
		cls.state = ca_disconnected;
	}

	ii = COM_CheckParm( "-listen" );
	if ( ii ) {
		if ( cls.state == ca_dedicated ) {
			Sys_Error( "Only one of -dedicated or -listen can be specified" );
		}
		if ( ii != ( com_argc - 1 ) ) {
			svs.maxclients = Q_atoi( com_argv[ ii + 1 ];
		}
		else {
			svs.maxclients = 8;
		}
	}
	if ( svs.maxclients < 1 ) {
		svs.maxclients = 8;
	}
	else if ( svs.maxclients > MAX_SCOREBOARD ) {
		svs.maxclients = MAX_SCOREBOARD;
	}
	svs.maxclientslimit = svs.maxclients;
	if ( svs.maxclientslimit < 4 ) {
		svs.maxclientslimit = 4;
	}
	svs.clients = (struct client_s *) Hunk_AllocName( svs.maxclientslimit * sizeof( client_t ), "clients" );

	if ( svs.maxclients > 1 ) {
		Cvar_SetQuick( &deathmatch, "1" );
	}
	else {
		Cvar_SetQuick( &deathmatch, "0" );
	}
}

void Host_Version_f( void ) {
	Con_Printf( "Quake Version %1.2f\n", VERSION );
	Con_Printf( "Quakespasm Version " QUAKESPASM_VER_STRING "\n" );
	Con_Printf( "Exe: " __TIME__ " " __DATE__ "\n" );
}

/* cvar callback functions */
void Host_Callback_Notify( cvar_t *var ) {
	if ( sv.active ) {
		SV_BroadcastPrintf( "\"%s\" changed to \"%s\"\n", var->name, var->string );
	}
}

/*
=============
Host_InitLocal
=============
*/
void Host_InitLocal( void ) {
	Cmd_AddCommand( "version", Host_Version_f );

	Host_InitCommands( );

	Cvar_RegisterVariable( &host_framerate );
	Cvar_RegisterVariable( &host_speeds );
	Cvar_RegisterVariable( &host_maxfps );	/* johnfitz */
	Cvar_SetCallback( &host_maxfps, Max_Fps_f );
	Cvar_RegisterVariable( &host_timescale ); /* johnfitz */

	Cvar_RegisterVariable( &max_edicts ); /* johnfitz */
	Cvar_SetCallback( &max_edicts, Max_Edicts_f );
	Cvar_RegisterVariable( &devstats ); /* johnfitz */

	Cvar_RegisterVariable( &sys_ticrate ); 
	Cvar_RegisterVariable( &sys_throttle );
	Cvar_RegisterVariable( &serverprofile );

	Cvar_RegisterVariable( &fraglimit );
	Cvar_RegisterVariable( &timelimit );
	Cvar_RegisterVariable( &teamplay );
	Cvar_SetCallback( &fraglimit, Host_Callback_Notify );
	Cvar_SetCallback( &timelimit, Host_Callback_Notify );
	Cvar_SetCallback( &teamplay, Host_Callback_Notify );
	Cvar_RegisterVariable( &samelevel );
	Cvar_RegisterVariable( &noexit );
	Cvar_SetCallback( &noexit, Host_Callback_Notify );
	Cvar_RegisterVariable( &skill );
	Cvar_RegisterVariable( &developers );
	Cvar_RegisterVariable( &coop );
	Cvar_RegisterVariable( &deathmatch );

	Cvar_RegisterVariable( &campaign );
	Cvar_RegisterVariable( &horde );
	Cvar_RegisterVariable( &sv_cheats );

	Cvar_RegisterVariable( &pausable );

	Cvar_RegisterVariable( &temp1 );

	Host_FindMaxClients( );
}

/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration( void ) {
	FILE	*ff;

	/* Dedicated servers initialize the host but don't parse and set the config.cfg cvars */
	if ( host_initialized && !isDedicated && !host_parms->errstate ) {
		ff = fopen( va( "%s/config.cfg", com_gamedir ), "w" );
		if ( !ff ) {
			Con_Printf( "Couldn't write config.cfg.\n" );
			return;
		}
		/* VID_SyncCvars(); */ /* johnfitz -- write actual current mode to config file, in case cvars were
			messed with */
		Key_WriteBindings( ff );
		Cvar_WriteVariables( ff );

		/* johnfitz -- Extra Commands to preserve state */
		fprintf( ff, "vid_restart\n" );
		if ( in_mlook.state & 1 ) {
			fprintf( ff, "+mlook\n" );
		}

		fclose( ff );
	}
}

/*
===============
SV_ClientPrintf
===============
*/
void SV_ClientPrintf( const char *format, ... ) {
	va_list		argptr;
	char		string[1024];

	va_start( argptr, format );
	
}


















