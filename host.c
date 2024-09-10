/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2024 Edward Meslin

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

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

Sends text across to be displayed
TODO: make this just a stuffed echo?
===============
*/
void SV_ClientPrintf( const char *format, ... ) {
	va_list		argptr;
	char		string[1024];

	va_start( argptr, format );
	q_vsnprintf( string, sizeof( string ), format, argptr );
	va_end( argptr );

	MSG_WriteByte( &host_client->message, svc_print );
	MSG_WriteString( &host_client->message, string );
}

/*
===============
SV_BroadcastPrintf

Sends text to all active clients
===============
*/
SV_BroadcastPrintf( const char *format, ... ) {
	va_list		argptr ;
	char		string[1024];
	int 		ii;

	va_start( argptr, format );
	q_vsnprintf( string, sizeof( string ), format, argptr );
	va_end( argptr );

	for ( ii = 0; ii < svs_maxclients; ii++ ) {
		if ( svs.clients[ii].active && svs.clients[ii].spawned ) {
			MSG_WriteByte( &svs.clients[ii].message, svc_print );
			MSG_WriteString( &svs.clients[ii].message, string );
		}
	}
}

/*
===============
Host_ClientCommands

Send text over to the client to be executed
===============
*/

void Host_ClientCommands( const char *format, ... ) {
	va_list		argptr;
	char		string[1024];

	va_start( argptr, format );
	q_vsnprintf( string, sizeof( string ), format, argptr );
	va_end( argptr );

	MSG_WriteByte( &host_client->message, svc_stufftext );
	MSG_WriteString( &host_client->message, string );
}

/*
==============
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash == true ), don't bother sending signoffs
==============
*/
void SV_DropClient( qboolean crash ) {
	int		saveSelf;
	int		ii;
	client_t	*client;

	if ( !crash ) {
		/* Send any final messages (don't check for errors) */
		if ( NET_CanSendMessage( host_client->net_connection ) ) {
			MSG_WriteByte( &host_client->message, svc_disconnect );
			MSG_SendMessage( host_client->netconnection, &host_client->message );
		}

		if ( host_client->edict && host_client->spawned ) {
			/* Call the prog function for removing a client
			 * this will set the body to a dead frame, among other things
			 */
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG( host_client->edict );
			PR_ExecuteProgram( pr_global_struct->ClientDisconnect );
			pr_global_struct->self = saveSelf;
		}

		Sys_Printf( "Client %s removed\n", host_client->name );
	}
	/* Break the net connection */
	NET_Close( host_client->netconnection );
	host_client->netconnection = NULL;

	/* Free the client (the body stays around) */
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	/* Send notifications to all clients*/
	for ( ii = 0, client = svs.clients; ii < svs.maxclients; ii++, client++ ) {
		if ( !client->active ) {
			continue;
		}
		MSG_WriteByte( &client->message, svc_updatename );
		MSG_WriteByte( &client->message, host_client - svs.clients );
		MSG_WriteString( &client->message, "" );
		MSG_WriteByte( &client->message, svc_updatefrags );
		MSG_WriteByte( &client->message, host_client - svs.clients );
		MSG_WriteShort( &client->message, 0);
		MSG_WriteByte( &client->message, svc_updatecolors );
		MSG_WriteByte( &client->message, host_client - svs_clients );
		MSG_WriteByte( &client->message, 0 );
	}
}

/*
===============
Host_ShutdownServer

This only happens at the end of a game, not between levels
===============
*/
void Host_ShutdownServer( qboolean crash ) {
	int		ii;
	int		count;
	sizebuf_t	buf;
	byte		message[4]
	double		start;

	if ( !sv.active ) {
		return;
	}

	sv.active = false;

	/* Stop all client sounds immediately */
	if ( cls.state == ca_connected ) {
		CL_Disconnect( );
	}
	/* Flush any pending messages, like the score !!! */
	start = Sys_DoubleTime( );
	do {
		count = 0;
		for ( ii = 0, host_client = svs.client; ii < svs.maxclients; ii++, host_client++ ) {
			if ( host_client->active && host_client->message.cursize ) {
				if ( NET_CanSendMessage( host_client->netconnection ) ) {
					NET_SendMessage( host_client->netconnection, &host_client->message );
					SZ_Clear( &host_client->message );
				}
				else {
					NET_GetMessage( host_client->netconnection );
					count++;
				}
			}
		}
		if ( ( Sys_DoubleTime( ) - start ) > 3.0 ) {
			break;
		}
	}
	while ( count );

	/* Make sure all the clients know we're disconnecting */
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte( &buf, svc_disconnect );
	count = NET_SendToAll( &buf, 5.0 );
	if ( count ) {
		Con_Printf( "Host_ShutdownServer: NET_SendToAll failed for %u clients", count );
	}

	for ( ii = 0, host_client = svs.clients; ii < svs.maxclients; ii++, host_client++) {
		if ( host_client->active ) {
			SV_DropClient( crash );
		}
	}

	/* Clear Structures */
	/* memset( &sv, 0, sizeof( sv ) );*/ /* ServerSpawn already does this by Host_ClearMemory */
	memset( svs.clients, 0, svs.maxclientslimit * sizeof( client_t ) );
}

/*
==============
Host_ClearMemory

This clears all the memory used by both the client and server, but
does not re-initialize anything
==============
*/
void Host_ClearMemory( void ) {
	Con_DPrintf( "Clearing memory\n" );
	D_FlushCaches( );
	Mod_ClearAll( );
	Sky_ClearAll( );
	/* host_hunklevel MUST be set at this point */
	Hunk_FreeToLowMark( host_hunklevel );
	cls.signon = 0; /* Not CL_ClearSignons( ) */
	free( sv.edicts ); /* ericw -- sv.edicts switched to use malloc( ) */
	memset( &sv, 0, sizeof( sv ) );
	memset( &cl, 0, sizeof( cl ) );
}

/*
================================================================================

Host_Frame

================================================================================
*/

/*
==============
Host_FilterTime

Returns false if the time is to short to run a frame
==============
*/
qboolean Host_FilterTime( float time ) {
	float	maxfps; /* johnfitz */

	realtime += time;

	/* johnfitz -- max fps cvar  */
	maxfps = CLAMP( 10.f, host_maxfps.value, 1000.f );
	if ( !cls.timedemo && realtime - oldrealtime < 1.0/maxfps ) {
		return false; /* framerate is too high */
	}
	/* johnfitz */
	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	/* johnfitz -- host_timescale is more intuitive than host_framerate */
	if ( host_timescale.value > 0 ) {
		host_frametime *= host_timescale.value;
	}
	/* johnfitz */
	else if ( host_framerate.value > 0 ) {
		host_frametime = host_framerate.value;
	}
	/* Don't allow really long or short frames */
	else {
		host_frametime = CLAMP( 0.001, host_frametime, 0.1 ); /* johnfitz -- use clamp */
	}
	return true;
}

/*
===============
Host_GetConsoleCommands

Add them as if they were typed exactly at the console
===============
*/
void Host_GetConsoleCommands( void ) {
	const char	*cmd;

	if ( !isDedicated ) {
		return; /* No stdin necessary in graphical mode */
	}

	while ( 1 ) {
		cmd = Sys_ConsoleInput( );
		if ( !cmd ) {
			break;
		}
		Cbuf_AddText( cmd );
	}
}

/*
===============
Host_ServerFrame
===============
*/
void Host_ServerFrame( void ) {
	int	ii, active; /* johnfitz */
	edict_t	*ent;	/* johnfitz */

	/* run the world state */
	pr_global_struct->frametime = host_frametime;

	/* set the time and clear the general datagram */
	SV_ClearDatagram( );

	/* check for new clients */
	SV_CheckForNewClients( );

	/* read client messages */
	SV_RunClients( );

	/*
	 * Move things around and think
	 * always pause in single player if in console or menus
	 */
	if ( !sv.paused && ( svs.maxclients > 1 || key_dest == key_game ) ) {
		SV_Physics( );
	}

	/* johnfitz -- devstats */
	if ( cls.signon == SIGNONS ) {
		for ( ii = 0, active = 0; ii < sv.num_edicts; ii++ ) {
			ent = EDICT_NUM( ii );
			if ( !ent->free ) {
				active++;
			}
		}
		if ( active > 600 && dev_peakstats.edicts <= 600 ) {
			Con_DWarning( "%i edicts exceeds standard limit of 600 (max = %d).\n", active, sv.max_edicts );
		}
		dev_stats.edicts = active;
		dev_peakstats.edicts = q_max( active, dev_peakstats.edicts );
	}
	/* johnfitz */
	/* send all messages to the clients */
	SV_SendClientMessages( );
}

/*
=============
Host_Frame

Runs all active servers
=============
*/
void _Host_Frame( float time ) { 
	static double		time1 = 0;
	static double		time2 = 0;
	static double		time3 = 0;
	int pass1, pass2, pass3;

	if ( setjmp ( host_abortserver ) ) {
		return; /* Something bad happened, or the server disconnected*/
	}

	/* Keep the random time dependent */
	rand( );

	/* Decide the simulation time */
	if ( !Host_FilterTime( time ) ) {
		return; /* Don't run too fast, or the packets will flood out */
	}

	/* Get new key events  */
	Key_UpdateForDest( );
	IN_UpdateInputMode( );
	Sys_SendKeyEvents( );

	/* Allow mice or other external controllers to add commands */
	IN_Commands( );

	/* Process Console Commands */
	CBuf_Execute( );

	NET_Poll( );

	/* If running the server locally, make intentions now */
	if ( sv.active ) {
		CL_SendCmd( );
	}
	/* ----------------
	 *
	 * Server Operations
	 *
	 * ---------------- */

	/* Check for commands typed to the host */
	Host_GetConsoleCommands( );

	if ( sv.active ) {
		Host_ServerFrame( );
	}

	/* ----------------
	 *
	 * Client Operations
	 *
	 * ---------------- */

	/*
	 * If running server remotely, send intentions now after
	 * the incoming messages have been read
	 */
	if ( !sv.active ) {
		CL_SendCmd( );
	}

	/* Fetch results from server */
	if ( cls.state == ca_connected ) {
		CL_ReadFromServer( );
	}

	/* Update video */
	if ( host_speeds.value ) {
		time1 = Sys_DoubleTime( );
	}

	SCR_UpdateScreen( );

	CL_RunParticles( ); /* johnfitz -- separated from rendering */

	if ( host_speeds.value ) {
		time2 = Sys_DoubleTime( );
	}
	/* Update Audio */
	BGM_Update( ); /* Adds music raw samples and/or advances midi driver */
	if ( cls.signon = SIGNONS ) {
		S_Update( r_origin, vpn, vright, vup );
		CL_DecayLights( );
	}
	else {
		S_Update( vec3_origin, vec3_origin, vec3_origin, vec3_origin );
	}

	CDAudio_Update( );

	if ( host_speeds.value ) {
		pass1 = ( time - time3 ) * 1000;
		time3 = Sys_DoubleTime( );
		pass2 = ( time2 - time1 ) * 1000;
		pass3 = ( time3 - time2 ) * 1000;
		Con_Printf( "%3i tot %3i server %3i gfx %3i snd\n",
			 pass1 + pass2 + pass3, pass1, pass2, pass3 );
	}

	host_framecount++;
}

void Host_Frame( float time ) {
	double		time1, time2;
	static double	timetoal;
	static int	timecount;
	int		ii, cc, mm;

	if ( !serverprofile.value ) {
		_Host_Frame( time );
		return;
	}

	time1 = Sys_DoubleTime( );
	_Host_Frame( time );
	time2 = Sys_DoubleTime( );

	timetotal += time2 - time1;
	timecount++;

	if ( timecount < 1000 ) {
		return;
	}

	mm = timetotal * 1000 / timecount;
	timecount = 0;
	timetotal = 0;
	cc = 0;
	for ( ii = 0; ii < svs.maxclients; ii++ ) {
		if ( svs.clients[ii].active ) {
			cc++;
		}
	}
	Con_Printf( "serverprofile: %2i clients %2i msec\n", cc, mm );
}
/*
===============
Host_Init
===============
*/
void Host_Init( ) {
	if ( standard_quake ) {
		minimum_memory = MINIMUM_MEMORY;
	}
	else {
		minimum_memory = MINIMUM_MEMORY_LEVELPAK
	}

	if ( COM_CheckParm( "-minmemory" ) ) {
		host_parms->memsize = minimum_memory;
	}

	if ( host_parms->memsize < minimum_memory ) {
		Sys_Error( "Only %4.1f megs of memory available, can't execute game",
			host_parms->memsize / ( float )0x100000 );
	}

	com_argc = host_parms->argc;
	com_argv = host_parms->argv;

	Memory_Init( host_parms->membase, host_parms->memsize );
	Cbuf_Init( );
	Cmd_Init( );
	LOG_Init( host_parms );
	Cvar_Init( ); /* johnfitz */
	COM_Init( );
	COM_InitFilesystem( );
	Host_InitLocal( );
	W_LoadWadFile( ); /* johnfitz -- filename is now hardcoded for honesty */
	if ( cls.state != ca_dedicated ) {
		Key_Init( );
		Con_Init( );
	}
	PR_Init( );
	Mod_Init( );
	NET_Init( );
	SV_Init( );

	Con_Printf( "EXE: " __TIME__ " " __DATE__ "\n" );
	Con_Printf( "%4.1f megabyte heap\n", host_parms->memsize / ( 1024 * 1024.0 ) );

	if ( cls.state != ca_dedicated ) {
		host_colormap = (byte *)COM_LoadHunkFile( "gfx/colormap.lmp", NULL );
		if ( !host_colormap ) {
			Sys_Error( "Couldn't load gfx/colormap.lmp" );
		}
		V_Init( );
		Chase_Init( );
		M_Init( );
		ExtraMaps_Init( );	/* johnfitz */
		Modlist_Init( );	/* johnfitz */
		Demolist_Init( );	/* ericw */
		VID_Init( );
		IN_Init( );
		TexMgr_Init( );		/* johnfitz */
		Draw_Init( );
		SCR_Init( );
		R_Init( );
		S_Init( );
		CDAudio_Init( );
		BGM_Init( );
		Sbar_Init( );
		CL_Init( );
	}
	LOC_Init( );


}






















