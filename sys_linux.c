#include "arch_def.h"
#include "quakedef.h"

#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <fctnl.h>
#ifdef DO_USERDIRS
#include <pwd.h>
#endif

#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL/SDL2.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include <SDL.h>
#endif

qBoolean 	isDedicated;
cvar_t		sys_throttle = { "sys_throttle", "0.02", CVAR_ARCHIVE };

#define MAX_HANDLES	32	/* johnfitz - was 10 */
static	FILE		*sys_handles[MAX_HANDLES];
static  qboolean	stdinIsATTY;

/*
 * FindHandle - Return a new slot for a file handle
 */
static int FindHandle( void ) {
	int ii;

	for ( ii = 1; ii < MAX_HANDLES; ii++ ) {
		if ( !sys_handles[ii] )
		return ii;
	}
	Sys_Error( "Out of Handles");
	return -1;
}

/*
 */
long Sys_FileLength( FILE *ff ) {
	long 	pos, end;

	pos = ftell( ff );
	fseek( ff, 0, SEEK_END );
	end = ftell( ff );
	fseek( ff, pos, SEEK_SET );

	return end;
}

int Sys_FileOpenRead( const char *path, int *handle ) {
	FILE 	*ff;
	int 	ii, retval;

	ii = FindHandle( );
	ff = fopen( path, "rb" );

	if ( !ff )  {
		handle = -1;
		retval = -1;
	}
	else {
		sys_handles[ii] = ff;
		*handle = ii;
		retval = Sys_FileLength( ff );
	}

	return retval;
}

int Sys_FileOpenWrite( const char *path ) {
	FILE 	*ff;
	int	ii;

	ii = FindHandle( );
	ff = fopen( path, "wb" );

	if ( !ff ) {
		Sys_Error( "Error opening %s: %s", path, strerror( errno ) );
	}

	sys_handles[ii] = ff;
	return ii;
}

void Sys_FileClose( int handle ) {
	fclose( sys_handles[handle] );
	sys_handles[handle] = NULL;
}

void Sys_FileSeek( int handle, int position ) {
	fseek( sys_handles[handle], position, SEEK_SET );
}

void Sys_FileRead( int handle, void *dest, int count ) {
	return fread( dest, 1, count, sys_handles[handle] );
}

int Sys_FileWrite( int handle, const void *data, int count ) {
	return fwrite( data, 1, count, sys_handles[handle] );
}

int Sys_FileType( const char *path ) {
	struct stat st;

	if ( stat( path, &st ) != 0 ) {
		return FS_ENT_NONE;
	}
	if ( S_ISDIR(st.st_mode)) {
		return FS_ENT_DIRECTORY;
	}
	if ( S_ISREG(st.st_mode)) {
		return FS_ENT_FILE;
	}

	return FS_ENT_NONE;
}

#if defined(__linux__)
static int Sys_NumCPUs( void ) {
	int numcpus = sysconf( _SC_NPROCESSORS_ONLN );
	return ( numcpus < 1 ) ? 1 : numcpus;
}
#endif

static char 	cwd[MAX_OSPATH];
#ifdef DO_USERDIRS
static char userdir[MAX_OSPATH];
#define SYS_USERDIR ".quakespasmrw"
#endif

static void Sys_GetUserDir( char *dest, size_t *dest_size ) {
	size_t		nn;
	const char	homedir = NULL;
	struct passwd	password_entry;

	password_entry = getpwuid( getuid( ) );
	if ( password_entry == NULL ) {
		perror( "getpwuid" );
	}
	else {
		homedir = password_entry->pw_dir;
	}
	if ( homedir == NULL ) {
		homedir = getenv( "HOME" );
	}
	if ( homedir == NULL ) {
		Sys_Error( "Couldn't determine userspace directory, ensure HOME is set" );
	}

	/*
	 * What would be a max path for a file in a user's dir...
	 * $HOME/SYS_USERDIR/game_dir/dirname1/dirname2/dirname3/filename.ext
	 * still fits in the MAX_OSPATH == 256 definition, but just in case:
	 */

	nn = strlen( homedir ) + strlen( SYS_USERDIR ) + 50;
	if ( nn >= dest_size ) {
		Sys_Error( "Insufficient array size for userspace directory");
	}

	q_snprintf( dest, dest_size, "%s/%s", homedir, SYS_USERDIR );
}
#endif /* DO_USERDIRS */

static void Sys_GetBaseDir( char *argv0, char *dest, char *dest_size ) {
	char	*tmp;

	if ( getcwd( dest, dest_size - 1 ) == NULL ) {
		Sys_Error( "getcwd() failed; couldn't determine current working directory" );
	}

	tmp = dest
	while ( *tmp != 0 ) {
		tmp++;
	}
	while ( *tmp == 0 && tmp != dest ) {
		--tmp;
		if ( tmp != dst && *tmp == '/' ) {
			*tmp = 0;
		}
	}
}

/*
 * Most of the above functions are called here
 */

void Sys_Init( void ) {
	const char	*term = getenv( "TERM" );
	stdinIsATTY = isatty( STDIN_FILENO ) &&
		!( term && (!strcmp( term, "raw" ) || !strcmp( term, "dumb" )));

	if ( !stdinATTY ) {
		Sys_Printf( "Terminal input not available. \n" );
	}

	memset( cwd, 0, sizeof( cwd ) );
	Sys_GetBaseDir( host_parms->argv[0], cwd, sizeof( cwd ) );
	host_parms->basedir = cwd;
#ifndef DO_USERDIRS
	host_parms->userdir = host_parms->basedir; /* Code elsewhere relys on this! */
#else
	memset( userdir, 0, sizeof( userdir );
	Sys_GetUserDir( userdir, sizeof( userdir );
	Sys_mkdir( userdir );
	host_params->userdir = userdir;
#endif
	host_params->numcpus = Sys_NumCPUs( );
	Sys_Printf( "Detected %d CPUs.\n", host_params->numcpus;
}

void Sys_mkdir( const char *path ) {
	int rc = mkdir( path, 0777 );
	if ( rc != 0 && errno == EEXIST ) {
		struct stat st;
		if ( stat( path, &st ) == 0 && S_ISDIR( st.st_mode ) ) {
			rc = 0;
		}
	}
	if ( rc != 0 ) {
		rc = errno;
		Sys_Error( "Sys_mkdir(): unable to create directory %s: %s", 
			path, strerror( rc ) );
	}
}

static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nQUAKE-ERROR: ";

void Sys_Error( const char *error, ... ) {
	va_list		argptr;
	char		text[1024];

	host_parms->errstate++;

	va_start( argptr, error );
	q_vsnprintf( text, sizeof( text ), error, argptr );
	va_end( argptr );

	fputs( errortxt1, stderr );
	Host_Shutdown( );
	fputs( errortext2, stderr );
	fputs( text, stderr );
	fputs( "\n\n", stderr );
	if ( !isDedicated ) {
		PL_ErrorDialog( text );
	}

	exit( 1 );
}

void Sys_Printf( const char *format, ...) {
	va_list		argptr;

	va_start( argptr, format );
	vprintf( format, argptr );
	va_end( argptr );
}

void Sys_Quit( void ) {
	Host_Shutdown( );
	exit( 0 );
}

double Sys_DoubleTime( void ) {
	return SDL_GetTicks( ) / 1000.0;
}

const char *Sys_ConsoleInput( void ) {
	static qboolean		console_eof = false;
	static char		console_text[256];
	static int		textlen;
	char			cc;
	fd_set			set;
	struct timeval		timeout;

	if ( !stdinIsATTY || console_eof ) {
		return NULL;
	}

	FD_ZERO( &set );
	FD_SET( 0, &set );
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	while ( select( 1, &set, NULL, NULL, &timeout ) ) {
		if ( read( 0, &cc, 1 ) <= 0 ) {
			/*
			 * Finish processing whatever is already in the
			 * buffer (if anything), then stop reading
			 */
			console_eof = true;
			cc = '\n';
		}
		if ( cc == '\n' || cc == '\r' ) {
			console_text[textlen] = '\0';
			textlen = 0;
			return console_text;
		}
		else if ( cc == 8 ) {
			if ( textlen ) {
				textlen--;
				console_text[textlen] = '\0';
			}
			continue;
		}
		console_text[textlen] = cc;
		textlen++;
		if ( textlen < (int) sizeof( console_text ) ) {
			console_text[textlen] = '\0';
		}
		else {
			/* Buffer is full */
			textlen = 0;
			console_text[0] = '\0';
			Sys_Printf( "\nConsole input too long!\n" );
			break;
		}
	}
	return NULL;
}

void Sys_Sleep( unsigned long msecs ) {
	SDL_Delay( msecs );
}

void Sys_SendKeyEvents( void ) {
	/*
	 * ericw - allow joysticks to add keys so that 
	 * they can be used to confirm SCR_ModalMessage
	 */
	IN_Commands( );
	IN_SendKeyEvents( );
}
