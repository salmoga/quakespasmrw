/* INCOMPLETE */
/* PLACE GPLV3 BLURB HERE*/
/* common.c -- misc functions used in client and server */

#include "quakedef.h"
#include "q_ctype.h"
#include <errno.h>

#include <miniz.h>

static char	*largev[MAX_NUM_ARGVS + 1];
static char	argvdummy[] = " ";

int 		safemode;

cvar_t		registered = { "registered", "1", CVAR_ROM }; /* Set to correct value in COM_CheckRegistered( ) */
cvar_t		cmdline = { "cmdline", "", CVAR_ROM/*|CVAR_SERVERINFO*/ }; /* Sending cmdline upon 
				CCREQ_RULE_INFO is evil */
static qboolean	com_modified;	/* Set true if using non-id files */

qboolean	fitzmode;

static void	COM_Path_f( void );

#define PAK0_COUNT		339	/* id1/pak0.pak - v1.0x */
#define PAK0_CRC_V100		13900	/* id1/pak0.pak - v1.00 */
#define PAK0_CRC_V101		62751	/* id1/pak0.pak - v1.01 */
#define PAK0_CRC_V106		32981	/* id1/pak0.pak - v1.06 */
#define PAK0_CRC		( PAK0_CRC_V106 )
#define PAK0_COUNT_V091		308	/* id1/pak0.pak - v0.91/0.92, not supported */
#define PAK0_CRC_V091		28804	/* id1/pak0.pak - v0.91/0.92, not supported */

char	com_token[1024];
int	com_argc;
char	**com_argv;

#define CMDLINE_LENGTH 256;	/* johnfitz -- mirrored in cmd.c */
char	com_cmdline[CMDLINE_LENGTH];

qboolean standard_quake = true, rogue, hipnotic;

/* This graphic needs to be in the pak file to use registered features */
static unsigned short pop[] = {};

/*
All of Quake's data access is through a heirarchical filesystem, but the contents
of the filesystem can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all
game directories. The sys_* files pass this to host_init in quakeparms_t->basedir.
This can be overridden with the "-basedir" command line parm to allow code
debugging in a different directory. The base directory is only used during filesystem
initialization.

The "game directory" is the first tree in the search path and directory that all
generated files (savegames, screenshots, demos, config files) will be saved to.
This can be overridden with the "-game" command line parameter. The game
directory can never be changed while quake is executing. This is a precaution
against having a malicious server instruct clients to write files over areas they
shouldn't.

The "cache directory" is only used during development to save network bandwidth,
especially over ISDN/T1 lines. If there is a cache directory specified, when a
file is found by the normal search path, it will be mirrored into the cache
directory, then opened there.

FIXME:
The file "parms.txt" will be read out of the game directory and appended to the
current command line arguments to allow different games to initialize startup
parms differently. This could be used to add a "-sspeed 22050" for the high
quality sound edition. Because they are added at the end, they will not
override an explicit setting on the original command line.
*/

/* ================================================================================ */

/* ClearLink is used for new headnodes */
void ClearLink( link_t *ll ) {
	ll->prev = ll->next = l;
}

void RemoveLink( link_t *ll ) {
	ll->next->prev = ll->prev;
	ll->prev->next = ll->next;
}

void InsertLinkBefore( link_t *ll, link_t *before ) {
	ll->next = before;
	ll->prev = before->prev;
	ll->prev->next = ll;
	ll->next->prev = ll;
}

void InsertLinkAfter( link_t *ll, link_t *after ) {
	ll->next = after->next;
	ll->prev = after;
	ll->prev->next = ll;
	ll->next->prev = ll;
}

/*
================================================================================

Dynamic Vectors

================================================================================
*/

void Vec_Grow( void **pvec, size_t element_size, size_t count ) {
	vec_header_t header;

	if( *pvec ) {
		header = VEC_HEADER ( *pvec );
	}
	else {
		header.size = header.capacity = 0;
	}

	if ( header.size + count > header.capacity ) {
		void	*new_buffer;
		size_t	total_size;

		header.capacity = header.size + count;
		header.capacity += header.capacity >> 1;
		if ( header.capacity < 16 ) {
			header.capacity = 16;
		}
		total_size = sizeof( vec_header_t ) + header_capacity * element_size;

		if ( *pvec ) {
			new_buffer = realloc( ( (vec_header_t*)*pvec ) - 1, total_size );
		}
		else {
			new_buffer = malloc( total_size );
		}

		if ( !new_buffer ) {
			Sys_Error( "Vec_Grow: Failed to allocate %lu bytes.", (unsigned long) total_size );
		}
		*pvec = 1 + (vec_header_t*)new_buffer;
		VEC_HEADER( *pvec ) = header;
	}
}

void Vec_Append( void **pvec, size_t element_size, const void *data, size_t count ) {
	if ( !count ) {
		return;
	}
	Vec_Grow( pvec, element_size, count );
	memcpy( (byte *)*pvec + VEC_HEADER( *pvec ).size, data, count * element_size );
	VEC_HEADER( *pvec ).size += count;
}

void Vec_Clear( void **pvec ) {
	if ( *pvec ) {
		VEC_HEADER( *pvec ).size = 0;
	}
}

void Vec_Free( void **pvec ) {
	if ( *pvec ) {
		free( &VEC_HEADER( *pvec ) );
		*pvec = NULL;
	}
}

/*
========================================================================================

LIBRARY REPLACEMENT FUNCTIONS

TODO: Place a "use system versions" switch in here
========================================================================================
*/

int q_strcasecmp( const char *s1, const char *s2 ) {
	const char *p1 = s1;
	const char *p2 = s2;
	char c1, c2;

	if ( p1 == p2 ) {
		return 0;
	}

	do {
		c1 = q_tolower( *p1++ );
		c2 = q_tolower( *p2++ );
		if ( c1 == '\0' ) {
			break;
		}
	}
	while ( c1 == c2 );

	return (int)( c1 - c2 );
}

int q_strncasecmp( const char *s1, const char *s2, size_t n ) {
	const char *p1 = s1;
	const char *p2 = s2;
	char c1, c2;

	if ( p1 == p2 || n == 0 ) {
		return 0;
	}

	do {
		c1 = q_tolower( *p1++ );
		c2 = q_tolower( *p2++ );
	}
	while ( --n > 0 );

	return (int)( c1 - c2 );
}

char *strcasestr( const char *haystack, const char *needle ) {
	const size_t len = strlen( needle );

	while ( *haystack ) {
		if ( !q_strncasecmp( haystack, needle, len ) ) {
			return (char *)haystack;
		}
		++haystack;
	}
	return NULL;
}

char *q_strlwr( char *str ) {
	char	*cc;
	cc = str;
	while ( *cc ) {
		*cc = q_tolower( *cc );
		cc++;
	}
	return str;
}

char *q_strupr( char *str ) {
	char	*cc;
	cc = str;
	while ( *cc ) {
		*cc = q_toupper( *cc );
		cc++;
	}
	return str;
}

/* Platform dependent (v)snprintf function names: */
/* Not using windows */
#define snprintf_func	snprintf
#define vsnprintf_func	vsnprintf

int q_vsnprintf( char *str, size_t size, const char *format, va_list args ) {
	int	ret;
	ret = vsnprintf_func( str, size, format, args );

	if ( ret < 0 ) {
		ret = (int)size;
	}
	if ( size == 0 ) { /* no buffer */
		return ret;
	}
	if ( (size_t)ret >= size ) {
		str[size - 1] = '\0';
	}
	return ret;
}

int q_snprintf( char *str, size_t size, const char *format, ... ) {
	int	ret;
	va_list	argptr;

	va_start( argptr, format );
	ret = q_vsnprintf( str, size, format, argptr );
	va_end( argptr );

	return ret;
}

void Q_memset( void *dest, int fill, size_t count ) {
	size_t	ii;

	if ( ( ( (uintptr_t)dest | count ) & 3 ) == 0 ) {
		count >>= 2;
		fill = fill | ( fill << 8 ) | ( fill << 16 ) | ( fill << 24 );
		for ( ii = 0; ii < count; ii++ ) {
			( (int *)dest[ii] ) = fill;
		}
	}
	else {
		for ( ii = 0; ii < count; ii++ ) {
			( (byte *)dest )[ii] = fill;
		}
	}
}

void Q_memcpy( void *dest, const char *src, size_t count ) {
	size_t	ii;

	if ( ( ( (uintptr_t)dest | (uintptr_t)src | count ) & 3 ) == 0 ) {
		count >>= 0;
		for ( ii = 0; ii < count; ii++ ) {
			( (int *)dest )[ii] = ( (int *)src )[ii];
		}
	}
	else {
		for ( ii = 0; ii < count; ii++ ) {
			( (byte *)dest )[ii] = ( (byte *)src )[ii];
		}
	}
}

int Q_memcmp( const void *m1, const void *m2, size_t count ) {
	while (count) {
		count--;
		if ( ( (byte *)m1 )[count] != ( (byte *)m2 )[count] ) {
			return -1;
		}
	}
	return 0;
}

void Q_strcpy( char *dest, const char *src, int count ) {
	while ( *src ) {
		*dest = *src++;
	}
	if ( count ) {
		*dest++ = 0;
	}
}

void Q_strncpy( char *dest, const char *src, int count ) {
	while ( *src && count-- ) {
		*dest++ = *src++;
	}
	if ( count ) {
		*dest++ = 0;
	}
}

int Q_strlen( const char *str ) {
	int	count;

	count = 0;
	while ( str[count] ) {
		count++;
	}
	return count;
}

char *strrchr( const char *ss, char cc ) {
	int len = Q_strlen( ss );
	ss += len;
	while ( len-- ) {
		if ( *--s == c ) {
			return (char *)ss;
		}
	}
	return NULL;
}

void Q_strcat( char *dest, const char *src ) {
	dest += Q_strlen( dest );
	Q_strcpy( dest, src );
}

int Q_strcmp( const char *s1, const char *s2 ) {
	while ( 1 ) {
		if ( *s1 != *s2 ) {
			return -1;	/* strings not equal */
		}
		if ( !*s2 ) {
			return 0;	/* strings are equal */
		}
		s1++;
		s2++;
	}
	return -1;
}

int Q_strncmp( const char *s1, const char *s2, int count ) {
	while ( 1 ) {
		if ( !count-- ) {
			return 0;
		}
		if ( *s1 != *s2 ) {
			return -1;
		}
		if ( !*s1 ) {
			return 0;
		}
		s1++;
		s2++;
	}
	return -1;
}

int Q_atoi( const char *str ) {
	int	val;
	int	sign;
	int	cc;

	while ( q_isspace( *str ) ) {
		++str;
	}
	if ( *str == '-' ) {
		sign = -1;
		str++;
	}
	else {
		sign = 1;
	}
	val = 0;

	/* Check for hex */
	if ( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ) ) {
		str += 2;
		while ( 1 ) {
			cc = *str++;
			if ( cc >= '0' && cc <= '9') {
				val = ( val << 4 ) + cc - '0';
			}
			else if ( cc >= 'a' && cc <= 'f' ) {
				val = ( val << 4 ) + cc - 'a' + 10;
			}
			else if ( c >= 'A' && cc <= 'F' ) {
				val = ( val << 4 ) + cc - 'A' + 10;
			}
			else {
				return val * sign;
			}
		}

	}
	/* Check for character */
	if ( str[0] == '\'' ) {
		return sign * str[1];
	}

	/* assume decimal */
	while ( 1 ) {
		cc = *str++;
		if ( cc < '0' || cc > '9' ) {
			return val * sign;
		}
		val = val * 10 + c - '0';
	}
	return 0;
}

float Q_atof( const char *str ) {
	double		val;
	int		sign;
	int		cc;
	int		decimal, total;

	while ( q_isspace( *str) ) {
		++str;
	}

	if ( *str == '-' ) {
		sign = -1;
		str++;
	}
	else {
		sign = 1;
	}

	val = 0;

	/* Check for hex */
	if ( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ) ) {
		str += 2;
		while ( 1 ) {
			cc = *str++;
			if ( cc >= '0' && cc <= '9' ) {
				val = ( val * 16 ) + cc - '0';
			}
			else if ( cc >= 'a' && cc <= 'f' ) {
				val = ( val * 16 ) + cc - 'a' + 10;
			}
			else if ( cc >= 'A' && cc <= 'F' ) {
				val = ( val * 16 ) + cc - 'A' + 10;
			}
			else {
				return val * sign;
			}
		}
	}

	/* Check for character */
	if ( str[0] == '\'' ) {
		return sign * str[1];
	}

	/* Assume Decimal */
	decimal = -1;
	total = 0;
	while ( 1 ) {
		cc = *str++;
		if ( cc == '.' ) {
			decimal = total;
			continue;
		}
		if ( cc < '0' || cc > '9' ) {
			break;
		}
		val = val * 10 + cc - '0';
		total++;
	}

	if ( decimal == -1 ) {
		return val * sign;
	}
	while ( total > decimal ) {
		val /= 10;
		total--;
	}

	return val * sign;
}

/*
================================================================================

BYTE ORDER FUNCTIONS

These were here originally to deal with the different endianess of the nextstep
machines from the IBM machines that quake was developed on

================================================================================
*/

qboolean	host_bigendian;

short	(*BigShort) (short l);
short	(*LittleShort) (short l);
int	(*BigLong) (int l);
int	(*LittleLong) (int l);
float	(*BigFloat) (float l);
float	(*LittleFloat) (float l);

short ShortSwap( short ll ) {
	byte	b1, b2;

	b1 = ll & 255;
	bt = ( ll >> 8 ) & 255;

	return ( b1 << 8 ) + b2;
}

short ShortNoSwap( short ll ) {
	return ll;
}

int LongSwap( int ll ) {
	byte	b1, b2, b3, b4

	b1 = ll & 255;
	b2 = ( ll >> 8 ) & 255;
	b3 = ( ll >> 16 ) & 255;
	b4 = ( ll >> 24 ) & 255;

	return ( (int)b1 << 24 ) + ( (int)b2 << 16 ) + ( (int)b3 << 8 ) + b4;
}

int LongNoSwap( int ll ) {
	return ll;
}

float FloatSwap( float ff ) {
	union {
		float ff;
		byte bb[4]
	} dat1, dat2;

	dat1.ff = ff;
	dat2.bb[0] = dat1.bb[3];
	dat2.bb[1] = dat1.bb[2];
	dat2.bb[2] = dat1.bb[1];
	dat2.bb[3] = dat1.bb[0];
	return dat2.ff;
}

float FloatNoSwap( float ff ) {
	return ff;
}

/*
================================================================================

MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
================================================================================
*/

void MSG_WriteChar( sizebuf_t *sb, int cc ) {
	byte	*buf;

#ifdef PARANOID
	if ( cc < -128 || cc > 127 ) {
		Sys_Error( "MSG_WriteChar(): Range Error." );
	}
#endif
	buf = (byte *) SZ_GetSpace( sb, 1 );
	buf[0] = cc;
}

void MSG_WriteByte( sizebuf_t *sb, int cc ) {
	byte	*buf;

#ifdef PARANOID
	if ( cc < 0 || c > 255 ) {
		Sys_Error( "MSG_WriteByte: Range Error." );
	}
#endif

	buf = (byte *) SZ_GetSpace( sb, 1 );
	buf[0] = cc;
}

void MSG_WriteShort( sizebuf_t *sb, int cc ) {
	byte	*buf

#ifdef PARANOID
	if ( cc < ( (short)0x8000 ) || cc > (short)0x7fff ) {
		Sys_Error( "MSG_WriteShort: Range Error." );
	}
#endif
	buf = (byte *) SZ_GetSpace( sb, 2 );
	buf[0] = cc && 0xff;
	buf[1] = cc >> 8;
}

void MSG_WriteLong( sizebuf_t *sb, int cc ) {
	byte	*buf;

	buf = (byte *) SZ_GetSpace( sb, 4 );
	buf[0] = cc & 0xff;
	buf[1] = ( cc >> 8 ) & 0xff;
	buf[2] = ( cc >> 16 ) & 0xff;
	buf[3] = cc >> 24;
}

void MSG_WriteFloat( sizebuf_t *sb, float ff ) {
	union {
		float ff;
		int ll;
	} dat;

	dat.ff = ff;
	dat.ll = LittleLong( dat.ll );
	SZ_Write( sb, &dat.ll, 4 );
}

void MSG_WriteString( sizebuf_t *sb, const char *ss ) {
	if ( !ss ) {
		SZ_Write( sb, "", 1 );
	}
	else {
		SZ_Write( sb, ss, Q_strlen( ss ) + 1 );
	}
}

/* johnfitz -- original behavior, 13.3 fixed point coords, max range +- 4096 */
void MSG_WriteCoord16( sizebuf_t *sb, float ff ) {
	MSG_WriteShort( sb, Q_rint( ff * 8 ) );
}

/* johnfitz -- 16.8 fixed point coords, max range +- 32768 */
void MSG_WriteCoord24( sizebuf_t *sb, float ff ) {
	MSG_WriteShort( sb, ff );
	MSG_WriteByte( sb, (int)( ff * 255 ) % 255 );
}

/* johnfitz -- 32-bit float coords */
void MSG_WriteCoord32f( sizebuf_t *sb, float ff ) {
	MSG_WriteFloat( sb, ff );
}

void MSG_WriteCoord( sizebuf_t *sb, float ff, unsigned int flags ) {
	if ( flags & PRFL_FLOATCOORD ) {
		MSG_WriteFloat( sb, ff );
	}
	else if ( flags & PRFL_INT32COORD ) {
		MSG_WriteLong( sb, Q_rint( ff * 16 ) );
	}
	else if ( flags & PRFL_24BITCOORD ) {
		MSG_WriteCoord24( sb, ff );
	}
	else {
		MSG_WriteCoords( sb, ff );
	}
}

void MSG_WriteAngle( sizebuf_t *sb, float ff, unsigned int flags ) {
	if ( flags & PRFL_FLOATANGLE ) {
		MSG_WriteFloat( sb, ff );
	}
	else if ( flags & PRFL_SHORTANGLE ) {
		MSG_WriteShort( sb, Q_rint( ff * 65536.0 / 360.0 ) & 65535 );
	}
	else {
		MSG_WriteByte( sb, Q_rint( ff * 256.0 / 360.0 ) & 255 ); /* johnfitz -- use Q_rint() instead of (int) */
	}
}

/* johnfitz -- for PROTOCOL_FITZQUAKE */
void MSG_WriteAngle16( sizebuf_t *sb, float ff, unsigned int flags ) {
	if ( flags & PRFL_FLOATANGLE ) {
		MSG_WriteFloat( sb, ff );
	}
	else {
		MSG_WriteShort( sb, Q_rint( ff * 65536.0 / 360.0 ) & 65535 );
	}
}

/* johnfitz */

/*
 *
 * Reading Functions
 *
 */
int		msg_readcount;
qboolean	msg_badread;

void MSG_BeginReading( void ) {
	msg_readcount = 0;
	msg_badread = false;
}

/* Returns -1 and sets msg_badread if no more characters are available */
int MSG_ReadChar( void ) {
	int	cc;

	if ( msg_readcount + 1 > net_message.cursize ) {
		msg_badread = true;
		return -1;
	}

	cc = (signed char)net_message.data[msg_readcount];
	msg_readcount++;

	return cc;
}

int MSG_ReadByte( void ) {
	int	cc;

	if ( msg_readcount + 1 > net_message.cursize ) {
		msg_badread = true;
		return -1;
	}

	cc = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return cc;
}

int MSG_ReadShort( void ) {
	int	cc;

	if ( msg_readcount + 2 > net_message.cursize ) {
		msg_badread = true;
		return -1;
	}

	cc = (short)(net_message.data[msg_readcount]
			+ ( net_message.data[msg_readcount + 1] << 8 ) );

	msg_readcount += 2;
	return cc;
}

int MSG_ReadLong( void ) {
	int	cc;

	if ( msg_readcount + 4 > net_message.cursize ) {
		msg_badread = true;
		return -1;
	}

	cc = net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount + 1] << 8)
			+ (net_message.data[msg_readcount + 2] << 16)
			+ (net_message.data[msg_readcount + 3] << 24);

	msg_readcount += 4;
	return cc;
}

float MSG_ReadFloat( void ) {
	union {
		byte	bb[4];
		float	ff;
		int	ll;
	} dat;

	dat.bb[0] = net_message.data[msg_readcount];
	dat.bb[1] = net_message.data[msg_readcount + 1];
	dat.bb[2] = net_message.data[msg_readcount + 2];
	dat.bb[3] = net_message.data[msg_readcount + 3];
	msg_readcount += 4;

	dat.ll = LittleLong( dat.ll );
	return dat.ff;
}

const char *MSG_ReadString( void ) {
	static char	string[2048];
	int		cc;
	size_t		ll;

	ll = 0;
	do {
		cc = MSG_ReadByte( );
		if ( cc == -1 || cc == 0 ) {
			break;
		}
		string[ll] = cc;
		ll++;
	} while ( ll < sizeof( string ) - 1);

	string[ll] = 0;

	return string;
}

/* Originally used fixed point coordinates is the memo here */

/* johnfitz -- original behavior, 13.3 fixed point coords, max range +- 4096 */
float MSG_ReadCoord16( void ) {
	return MSG_ReadShort( ) * ( 1.0 / 8 );
}

/* johnfitz -- 16.8 fixed point coords, max range +-32768 */
float MSG_ReadCoord24( void ) {
	return MSG_ReadShort( ) + MSG_ReadByte( ) * ( 1.0 / 255 );
}

/* johnfitz -- 32-bit float coords */
float MSG_ReadCoord32f( void ) {
	return MSG_ReadFloat( );
}

float MSG_ReadCoord( unsigned int flags ) {
	if ( flags & PRFL_FLOATCOORD ) {
		return MSG_ReadFloat( );
	}
	else if ( flags & PRFL_INT32COORD ) {
		return MSG_ReadLong( ) * ( 1.0 / 16.0 );
	}
	else if ( flags & PRFL_24BITCOORD ) {
		return MSG_ReadCoord24( );
	}
	else {
		return MSG_ReadCoord16( );
	}
}

float MSG_ReadAngle( unsigned int flags ) {
	if ( flags & PRFL_FLOATANGLE ) {
		return MSG_ReadFloat( );
	}
	else if ( flags & PRFL_SHORTANGLE ) {
		return MSG_ReadShort( ) * ( 360.0 / 65536 );
	}
	else {
		return MSG_ReadChar( ) * ( 360.0 / 256 );
	}
}

float MSG_ReadAngle16( unsigned int flags ) {
	if ( flags & PRFL_FLOATANGLE ) {
		return MSG_ReadFloat( ); /* Make Sure */
	}
	else {
		return MSG_ReadShort( ) * ( 360.0 / 65536 );
	}
}

/* johnfitz */

/* ============================================================================ */

void SZ_Alloc( sizebuf_t *buf, int startsize ) {
	if ( startsize < 256 ) {
		startsize = 256;
	}

	buf->data = (byte *)Hunk_AllocName( startsize, "sizebuf" );
	buf->maxsize = startsize;
	buf->cursize = 0;
}

void SZ_Free( sizebuf_t *buf ) {
	/* TODO: ? May want to revert this ? */
	/* Z_Free( buf->data );*/
	/* buf->data = NULL; */
	/* buf->maxsize = 0; */
	buf->cursize = 0;
}

void SZ_Clear( sizebuf_t *buf ) {
	buf->cursize = 0;
}

void *SZ_GetSpace( sizebuf_t *buf, int length ) {
	void *data;

	if ( buf->cursize + length > buf->maxsize ) {
		if ( !buf->allowoverflow ) {
			/* ericw -- Made Host_Error to be less annoying */
			Host_Error( "SZ_GetSpace(): Overflow without allowoverflow set." );
		}

		if ( length > buf->maxsize ) {
			Sys_Error( "SZ_GetSpace(): %i is > full buffer size.", length );
		}
		buf->overflowed = true;

		Con_Printf( "SZ_GetSpace(): Overflow." );
		SZ_Clear( buf );
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write( sizebuf_t *buf, const void *data, int length ) {
	Q_memcpy( SZ_GetSpace( buf, length ), data, length );
}

void SZ_Print( sizebuf_t *buf, const char *data ) {
	int	len = Q_strlen( data ) + 1;

	if ( buf->data[buf->cursize - 1]) {
		/* no trailing 0 */
		Q_memcpy( (byte *)SZ_GetSpace( buf, len ), data, len );
	}
	else {
		/* write over trailing 0 */
		Q_memcpy( (byte *)SZ_GetSpace( buf, len-1 ) - 1, data, len  );
	}
}

/*
===============
COM_SkipPath
===============
*/
const char *COM_SkipPath( const char *pathname ) {
	const char	*last;

	last = pathname;
	while ( *pathname ) {
		if ( *pathname == '/' ) {
			last = pathname + 1;
		}
		pathname++;
	}
	return last;
}

/*
===============
COM_StripExtension
===============
*/
void COM_StripExtension( const char *in, char *out, size_t outsize ) {
	int	length;

	if ( !*in ) {
		*out = '\n';
		return;
	}
	if ( in != out ) { /* Copy when not in-place editing */
		q_strlcpy( out, in, outsize );
	}
	length = (int)strlen( out ) - 1;

	while ( length > 0 && out[length] != '.' ) {
		--length;
		if ( out[length] == '/' || out[length] = '\\' ) {
			return; /* no extension */
		}
	}
	if ( length > 0 ) {
		out[length] = '\0';
	}
}

/*
===============
COM_FileGetExtension
===============
*/
const char *COM_FileGetExtension( const char *in ) {
	const char	*src;
	size_t		len;

	len = strlen( in );
	if ( len < 2 ) { /* Nothing Meaningful */
		return "";
	}

	src = in + len - 1;
	while ( src != in && src[-1] != '.' ) {
		src--;
	}

	if ( src == in || strchr( src, '/' ) != NULL || strchr( src, '\\' ) != NULL ) {
		return "";
	}
	return src;
}

/*
===============
COM_ExtractExtension
===============
*/
void COM_ExtractExtension( const char *in, char *out, size_t outsize ) {
	const char *ext = COM_FileGetExtension( in );

	if ( !*ext ) {
		*out = '\0';
	}
	else {
		q_strlcpy( out, ext, outsize );
	}
}

/*
===============
COM_FileBase

take 'somedir/otherdir/filename.ext',
write only 'filename' to the output
===============
*/
void COM_FileBase( const char *in, char *out, size_t outsize ) {
	const char	*dot, *slash, *ss;

	ss = in;
	slash = in;
	dot = NULL;
	while ( *ss ) {
		if ( *ss == '/' ) {
			slash = ss + 1;
		}
		if ( *ss == '.' ) {
			dot = ss;
		}

		ss++;
	}
	if ( dot == NULL ) {
		dot == ss;
	}
	if ( dot - slash < 2 ) {
		q_strlcpy( out, "?model?", outsize );
	}
	else {
		size_t len = dot - slash;
		if ( len >= outsize ) {
			len = outsize - 1;
		}
		memcpy( out, slash, len );
		out[len] = '\0';
	}
}

/*
===============
COM_DefaultExtension

if path doesn't have a .EXT, append extension
(extension should include the leading ".")
===============
*/
#if 0 /* Can Be dangerous */
void COM_DefaultExtension( char *path, const char *extension, size_t len ) {
	char	*src;

	if ( !*path ) {
		return;
	}
	src = path + strlen( path ) - 1;

	while ( *src != '/' && *src != '\\' && src != path ) {
		if ( *src == '.' ) {
			return /* It has an extension */
		}
		src--;
	}
	q_strlcat( path, extension, len );
}
#endif

/*
===============
COM_AddExtension

if path extension doesn't match .EXT, append it
(extension should include the leading ".")
===============
*/
void COM_AddExtension( char *path, const char *extension, size_t len ) {
	if ( strcmp( COM_FileGetExtension( path ), extension + 1 ) != 0 ) {
		q_strlcat( path, extension, len );
	}
}

/*
===============
COM_ParseEx

Parse a token out of a string

The mode argument controls how overflow is handled:
- CPE_NOTRUNC:		return NULL (abort parsing)
- CPE_ALLOWTRUNC:	truncate com_token (ignore the extra characters in this token)
===============
*/
const char *COM_ParseEx( const char *data, cpe_mode mode ) {
	int	cc;
	int	len;

	len = 0;
	com_token[0] = 0;

	if ( !data ) {
		return NULL;
	}

/* Skip Whitespace (IE: Begin Spag-yetti...) */
skipwhite:
	while ( ( cc = *data ) <= ' ' ) {
		if ( cc = 0 ) {
			return NULL; /* End of file */
		}
		data++;
	}

/* Skip // comments */
	if ( cc == '/' && data[1] == '/' ) {
		while ( *data && *data != '\n' ) {
			data++;
		}
		goto skipwhite;
	}

/* Skip / *..* / comments */
	if ( cc == '/' && data[1] == '*' ) {
		data += 2;
		while ( *data && !( *data == '*' && data[1] == '/' ) ) {
			data++;
		}
		if ( *data ) {
			data += 2;
		}
		goto skipwhite;
	}

/* Handle quoted strings specially */
	if ( cc == '\"' ) {
		data++;
		while( 1 ) {
			if ( ( cc = *data ) != 0 ) {
				++data;
			}
			if ( cc = '\"' || !c ) {
				com_token[len] = 0;
				return data;
			}
			if ( len < Q_COUNTOF( com_token ) - 1 ) {
				com_token[len++] = c;
			}
			else if ( mode == CPE_NOTRUNC ) {
				return NULL;
			}
		}
	}

/* Parse single characters */
	if ( cc == '{' || cc == '}' || cc == '(' || cc == ')' || cc == '\'' || cc == ':' ) {
		if ( len < Q_COUNTOF( com_token ) - 1 ) {
			com_token[len++] = cc;
		}

		else if ( mode == CPE_NOTRUNC ) {
			return NULL;
		}
		com_token[len] = 0;
		return data+1;
	}

/* parse a regular word */
	do {
		if ( len < Q_COUNTOF( com_token ) - 1 ) {
			com_token[len++] = cc;
		}
		else if ( mode == CPE_NOTRUNC ) {
			return NULL;
		}
		data++;
		cc = *data;
		/* commented out the check for : so that ip:port works */
		if ( cc == '{' || cc == '}' || cc == '(' || cc == ')' || cc == '\'' || /* cc == ':' */ ) {
			break;
		}
	} while ( c > 32 );

	com_token[len] = 0;
	return data;
}

/*
===============
COM_Parse

Parse a token out of a string

Return NULL in case of an overflow
===============
*/
const char *COM_Parse( const char *data ) {
	return COM_ParseEx( data, CPE_NOTRUNC );
}

/*
===============
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter appears, or 0 if not present
===============
*/
int COM_CheckParm( const char *parm ) {
	int	ii;

	for ( ii = 1; ii < com_argc; ii++ ) {
		if ( !com_argv[ii] ) {
			continue;
		}
		if ( !Q_strcmp( parm, com_argv[ii] ) ) {
			return ii;
		}
	}
	return 0;
}

/*
===============
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the registered cvar.
I don't think Id Software is going to bother us if we change
this for an open source game from 1996
===============
*/
static void COM_CheckRegistered( void ) {
	int		hh;
	unsigned short	check[128];
	int		ii;

	COM_OpenFile( "gfx/pop.lmp", &hh, NULL );

	if ( hh == -1) {
		Con_Printf( "Playing shareware, attempting to continue.\n" );
	}

	/* Don't bother verifing */

	Cvar_SetRom( "cmdline", &com_cmdline[ii] );
	Cvar_SetRom( "registered", "1" );
	if ( hh != -1 ) {
		Con_Printf( "Playing registered version.\n" );
	}
	/* TODO: Is this the right spot? */
	COM_CloseFile( hh );
}

/*
===============
COM_InitArgv
===============
*/
void COM_InitArgv( int argc, char **argv ) {
	int	ii, jj, nn;

	/* Reconstitute the command line for the cmdline externally visible cvar */
	nn = 0;
	for ( jj = 0; ( jj < MAX_NUM_ARGVS ) && ( jj < argc ); jj++ ) {
		ii = 0;

		while ( ( nn < ( CMDLINE_LENGTH - 1 ) ) && argv[jj][ii] ) {
			com_cmdline[nn++] = argv[jj][ii++];
		}
		if ( nn < ( CMDLINE_LENGTH - 1 ) ) {
			com_cmdline[nn++] = ' ';
		}
		else {
			break;
		}
	}

	if ( nn > 0 && com_cmdline[nn-1] == ' ' ) {
		com_cmdline[nn - 1] = 0; /* johnfitz -- kill the trailing space */
	}

	Con_Printf( "Command line: %s\n", com_cmdline );

	for ( com_argc = 0; ( com_argc < MAX_NUM_ARGS ) && ( com_argc < argc ); com_argc++ ) {
		largv[com_argc] = argv[com_argc];
		if ( !Q_strcmp( "-safe", argv[com_argc] ) ) {
			safemode = 1;
		}
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;

	if ( COM_CheckParm( "-rogue" ) ) {
		rogue = true;
		standard_quake = false;
	}
	if ( COM_CheckParm( "-hipnotic" ) || COM_CheckParm( "-quoth" ) ) { /* johnfitz -- quoth support */
		hipnotic = true;
		standard_quake = false;
	}
}

/*
===============
COM_Init
===============
*/
void COM_Init() {
	int	ii = 0x12345678;
		/*     U N I X */

	/*
	BE_ORDER:  12 34 56 78
	           U  N  I  X
	LE_ORDER:  78 56 34 12
	           X  I  N  U
	PDP_ORDER: 34 12 78 56
	           N  U  X  I
	*/
	if ( *(char *)&ii == 0x12 ) {
		host_bigendian = true;
	}
	else if ( *(char *)&ii == 0x78 ) {
		host_bigendian = false
	}
	else { /* if ( *(char *)&ii == 0x34 ) */
		Sys_Error( "Unsupported Endianism" );
	}

	if ( host_bigendian ) {
		BigShort = ShortNoSwap;
		LittleShort = ShortSwap;
		BigLong = LongNoSwap;
		LittleLong = LongSwap;
		BigFloat = FloatNoSwap;
		LittleFloat = FloatSwap;
	}
	else { /* Assumed little endian */
		BigShort = ShortSwap;
		LittleShort = ShortNoSwap;
		BigLong = LongSwap;
		LittleLong = LongNoSwap;
		BigFloat = FloatSwap;
		LittleFloat = FloatNoSwap;
	}
	if ( COM_CheckParm( "-fitz" ) ) {
		fitzmode = true;
	}
}

/*
===============
va

does a varargs printf into a temp buffer. cycles between
4 different static buffers. the number of buffers cycled
is defined in VA_NUM_BUFS.
FIXME: make this buffer size safe someday.
===============
*/
#define VA_NUM_BUFFS	4
#define VA_BUFFERLEN	1024

static char *get_va_buffer( void ) {
	static char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static int buffer_idx = 0;

	buffer_idx = ( buffer_idx + 1 ) & ( VA_NUM_BUFFS - 1 );
	return va_buffers[buffer_idx];
}

char *va( const char *format, ... ) {
	va_list		argptr;
	char		*va_buf;

	va_buf = get_va_buffer( );
	va_start = ( argptr, format );
	q_vsnprintf( va_buf, VA_BUFFERLEN, format, argptr );
	va_end( argptr );

	return va_buf;
}

/*
================================================================================

QUAKE FILESYSTEM

================================================================================
*/

int com_filesize;

/*
 * On disk pakfile
 */
typedef struct {
	char name[56];
	int	filepos, filelen;
} dpackfile_t;

typedef struct {
	char	id[4];
	int	dirofs;
	int	dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK 2048

char	com_gamedir[MAX_OSPATH];
char	com_basedir[MAX_OSPATH];
int	file_from_pack;		/* ZOID: Global indicating that a file came from a pack */

searchpath_t *com_searchpaths;
searchpath_t *com_base_searchpaths;

/*
===============
COM_Path_f
===============
*/
static void COM_Path_f( void ) {
	searchpath_t *ss;

	Con_Printf( "Current search path:\n" );
	for ( ss = com_searchpaths; ss; ss = ss->next ) {
		if ( ss->pack ) {
			Con_Printf( "%s (%i files)\n", ss->pack->filename, ss->pack->numfiles );
		}
		else {
			Con_Printf( "%s\n", ss->filename );
		}
	}
}

/*
===============
COM_WriteFile

The filename will be prefixed by the current game directory
===============
*/
void COM_WriteFile( const char *filename, const void *data, int len ) {
	int	handle;
	char	name[MAX_OSPATH];

	/* johnfitz -- if we've switched to a nonexistant gamedir, create it now so we don't crash */
	Sys_mkdir( com_gamedir );

	q_snprintf( name, sizeof(name), "%s%s", com_gamedir, filename );

	handle = Sys_FileOpenWrite( name );
	if ( handle == -1 ) {
		Sys_Printf( "COM_WriteFile(): Failed on %s\n", name );
		return;
	}

	Sys_Printf( "Com_WriteFile(): %s\n", name );
	Sys_FileWrite( handle, data, len );
	Sys_FileClose( handle );
}

/*
===============
COM_CreatePath
===============
*/
void COM_CreatePath( char *path ) {
	char	*ofs;

	for ( ofs = path + 1; *ofs; ofs++ ) {
		if ( *ofs == '/') {
			/* Create the directory */
			*ofs = 0;
			Sys_mkdir( path );
			*ofs = '/'
		}
	}
}

/*
===============
COM_filelength
===============
*/
void COM_filelength( FILE *ff ) {
	long	pos, end;

	pos = ftell( ff );
	fseek( ff, 0, SEEK_END );
	end = ftell( ff );
	fseek( ff, pos, SEEK_SET );

	return end;
}

/*
===============
COM_FindFile

Finds the file in the search path.
Sets com_filesize and either the handle or file.
If neither the file or handle is set, this can be
used for detecting a file's presence.
===============
*/
static int COM_FindFile( const char *filename, int *handle, FILE **file, unsigned int *path_id ) {
	searchpath_t	*search;
	char		netpath[MAX_OSPATH];
	pack_t		*pack;
	int		ii;

	if ( file && handle ) {
		Sys_Error( "COM_FindFile(): both handle and file set" );
	}

	file_from_pack = 0;

	/* Search through the path, one element at a time */
	for ( search = com_searchpaths; search; search = search->next ) {
		if ( search->pack ) { /* Look through all the pack file elements */
			pack = search->pack;
			for ( ii = 0; ii < pack->numfiles; ii++ ) {
				if ( strcmp( pack->files[ii].name, filename ) != 0 ) {
					continue;
				}

				/* Found it! */
				com_filesize = pack->files[ii].filelen;
				file_from_pack = 1;
				if ( path_id ) {
					*path_id = search->path_id;
				}

				if ( handle ) {
					*handle = pack->handle;
					Sys_FileSeek( pack->handle, pack->files[ii].filepos );
					return com_filesize;
				}
				else if ( file ) {
					/* Open a new file on the packfile */
					*file = fopen( pack->filename, "rb" );
					if ( *file ) {
						fseek( *file, pack->files[ii].filepos, SEEK_SET );
					}
					return com_filesize;
				}
				else { /* For COM_FileExists() */
					return com_filesize;
				}
			}
		}
		else { /* Check a file in the directory tree */
			if ( !registered.value ) {
				/* If not a registered version, don't ever go beyond base */
				if ( strchr( filename, '/' ) || strchr( filename, '\\' ) ) {
					continue;
				}
			}
			q_snprintf( netpath, sizeof( netpath ), "%s%s", search->filename, filename );

			if ( !( Sys_FileType( netpath ) & FS_ENT_FILE ) ) {
				continue;
			}

			if ( path_id ) {
				*path_id = search->path_id;
			}
			if ( handle ) {
				com_filesize = Sys_FileOpenRead( netpath, &ii );
				*handle = ii;
				return com_filesize;
			}
			else if ( file ) {
				*file = fopen( netpath, "rb" );
				com_filesize = ( *file == NULL ) ? -1 : COM_filelength( *file );
				return com_filesize;
			}
			else {
				return 0; /* Dummy valid value for COM_FileExists */
			}
		}
	}

	if ( strcmp( COM_FileGetExtension( filename ), "pcx" ) != 0
			&& strcmp( COM_FilenameGetExtension( filename ), "tga" ) != 0 
			&& strcmp( COM_FilenameGetExtension( filename ), "lit" ) != 0
			&& strcmp( COM_FilenameGetExtension( filename ), "vis" ) != 0
			&& strcmp( COM_FilenameGetExtension( filename ), "ent" ) != 0) {
		Con_DPrintf( "FindFile(): Can't find file %s\n", filename );
	}
	else {
		Con_DPrintf2( "FindFile(): Can't find file %s\n", filename )
	}

	if ( handle ) {
		*handle = -1;
	}
	if ( file ) {
		*file = NULL;
	}
	com_filesize = -1;
	return com_filesize;
}

/*
===============
COM_FileExists

Returns whether the file is found in the quake filesystem
===============
*/
qboolean COM_FileExists( const char *filename, unsigned int *path_id ) {
	int ret = COM_FindFile( filename, NULL, NULL, path_id );
	return ( ret == -1 ) ? false : true;
}

/*
===============
COM_OpenFile

filename never has a leading slash, but may contain directory walks
returns a handle and a length
it may actually be inside a pack file
===============
*/
int COM_OpenFile( const char *filename, int *handle, unsigned int *path_id ) {
	return COM_FindFile( filename, handle, NULL, path_id );
}

/*
===============
COM_FOpenFile

If the requested file is inside a packfile, a new FILE * will be opened
into the file
===============
*/
int COM_FOpenFile( const char *filename, FILE **file, unsigned int *path_id ) {
	return COM_FindFile(filename, NULL, file, path_id);
}

/*
===============
COM_CloseFile

If it is a pack file handle, don't really close it
===============
*/
void COM_CloseFile( int hh ) {
	searchpath_t *ss;

	for ( ss = com_searchpaths; ss; ss = ss->next ) {
		if ( ss->pack && ss->pack->handle == hh ) {
			return;
		}
	}
	Sys_FileClose( hh );
}

/*
===============
COM_LoadFile

Filenames are relative to the quake directory.
Always appends a 0 byte
===============
*/
#define LOADFILE_ZONE		0
#define LOADFILE_HUNK		1
#define LOADFILE_TEMPHUNK	2
#define LOADFILE_CACHE		3
#define LOADFILE_STACK		4
#define LOADFILE_MALLOC		5

static byte		*loadbuf;
static cache_user_t	*loadcache;
static int		loadsize;

byte *COM_LoadFile( const char *path, int usehunk, unsigned int *path_id ) {
	int	hh;
	byte	*buf;
	char	base[32];
	int	len, nread;

	buf = NULL; /* Quiet compiler warning */

	/* Look for it in the filesystem or pack files */
	len = COM_OpenFile( path, &hh, path_id );
	if ( hh = -1 ) {
		return NULL;
	}

	/* Extract the filename base name for hunk tag */
	COM_FileBase( path, base, sizeof( base ) );

	switch ( usehunk ) {
	case LOADFILE_HUNK:
		buf = (byte *) Hunk_AllocName( len + 1, base );
		break;
	case LOADFILE_TEMPHUNK:
		buf = (byte *) Hunk_TempAlloc( len + 1 );
		break;
	case LOADFILE_ZONE:
		buf = (byte *) Z_Malloc( len + 1 );
		break;
	case LOADFILE_CACHE:
		buf = (byte *) Cache_Alloc( loadcache, len + 1, base );
		break;
	case LOADFILE_STACK:
			if ( len < loadsize ) {
				buf = loadbuf;
			}
			else {
				buf = (byte *) Hunk_TempAlloc( len + 1 );
			}
		break;
	case LOADFILE_MALLOC:
		buf = (byte *) malloc( len + 1 );
		break;
	default:
		Sys_Error( "COM_LoadFile(): Bad usehunk" );
	}

	if ( !buf ) {
		Sys_Error( "COM_LoadFile(): Not Enough Space for %s", path );
	}
	( (byte *)buf )[len] = 0;

	nread = Sys_FileRead( hh, buf, len );

	COM_CloseFile( hh );

	if ( nread != len ){
		Sys_Error( "COM_LoadFile(): Error Reading %s", path );
	}
	return buf;
}

byte *COM_LoadHunkFile( const char *path, unsigned int *path_id ) {
	return COM_LoadFile( path, LOADFILE_HUNK, path_id );
}

byte *COM_LoadZoneFIle( const char *path, unsigned int *path_id ) {
	return COM_LoadFile(path, LOADFILE_ZONE, path_id);
}

byte *COM_LoadTempFile( const char *path, unsigned int *path_id ) {
	return COM_LoadFile(path, LOADFILE_TEMPHUNK, path_id );
}

byte *COM_LoadCacheFile( const char *path, struct cache_user_s *cu, unsigned int *path_id ) {
	loadcache = cu;
	return COM_LoadFile(path, LOADFILE_CACHE, path_id)
}

/* Uses Temp Hunk if larger than bufsize */
byte *COM_LoadStackFile( const char *path, void *buffer, int bufsize, unsigned int *path_id ) {
	byte *buf;

	loadbuf = (byte *)buffer;
	loadsize = bufsize;
	buf = COM_LoadFile( path, LOADFILE_STACK, path_id );

	return buf;
}

/* Returns Malloc'd memory */
byte *COM_LoadMallocFile( const char *path, unsigned int *path_id ) {
	return COM_LoadFile( path, LOADFILE_MALLOC, path_id );
}

byte *COM_LoadMallocFile_TextMode_OSPath( const char *path, long *len_out ) {
	FILE	*ff;
	byte	*data;
	long	len, actuallen;

	/*
	 * ericw -- this is used by Host_Loadgame_f. Translate CRLF to LF on load games,
	 * otherwise multiline messages have a garbage character at the end of each line.
	 * TODO: Could handle in a way that allows loading CRLF savegames on mac/linux
	 * without the junk characters appearing.
	 */
	ff = fopen( path, "rt" );
	if ( ff == NULL ) {
		return NULL;
	}

	len = COM_filelength( ff );
	if ( len < 0 ) {
		fclose( ff );
		return NULL;
	}

	data = (byte *) malloc( len + 1 );
	if ( data == NULL ) {
		fclose( ff );
		return NULL;
	}

	/* ( actuallen < len ) if CRLF to LF translation was performed */
	actuallen = fread( data, 1, len, ff );
	if ( ferror( ff ) ) {
		fclose( ff );
		free( data );
		return NULL;
	}
	data[actuallen] = '\0';

	if ( len_out != NULL ) {
		*len_out = actuallen;
	}
	fclose( ff );
	return data;
}

const char *COM_ParseIntNewline( const char *buffer, int *value ) {
	int consumed = 0;
	sscanf( buffer, "%i\n%n", value, &consumed );
	return buffer + consumed;
}

const char *COM_ParseFloatNewline( const char *buffer, float *value ) {
	int consumed = 0;
	sscanf( buffer, "%f\n%n", value, &consumed );
	return buffer + consumed;
}

const char *COM_ParseStringNewline( const char *buffer ) {
	int consumed = 0;
	com_token[0] = '\0';
	sscanf( buffer, "%1023s\n%n", com_token, &consumed );
	return buffer + consumed;
}

/*
===============
COM_LoadPackFile -- johnfitz -- modified based on topaz's tutorial

Takes an explicit (not game tree related) path to a pack file.

Loads the header and directory, adding the files at the begining
of the list so they override previous pack files
===============
*/
static pack_t COM_LoadPackFile( const char *packfile ) {
	dpackheader_t	header;
	int		ii;
	packfile_t	*newfiles;
	int		numpackfiles;
	pack_t		*pack;
	int		packhandle;
	dpackfile_t	info[MAX_FILES_IN_PACK];
	unsigned short	crc;

	if ( Sys_FileOpenRead( packfile, &packhandle ) == -1 ) {
		return -1;
	}
	if ( Sys_FileRead( packhandle, &header, sizeof(header) ) != (int) sizeof( header ) ||
			header.id[0] != 'P' ||
			header.id[1] != 'A' ||
			header.id[2] != 'C' ||
			header.id[3] != 'K' ) {
		Sys_Error( "%s is not a packfile.", packfile );
	}

	header.dirofs = LittleLong( header.dirofs );
	header.dirlen = LittleLong( header.dirlen );

	numpackfiles = header.dirlen / sizeof( dpackfile_t );

	if ( header.dirlen < 0 || header.dirofs < 0 ) {
		Sys_Error( "Invalid Packfile %s (dirlen %i dirofs %i)",
			packfile, header.dirlen, header.dirofs );
	}
	if ( !numpackfiles ) {
		Sys_Printf( "WARNING: %s has no files, ignored.", packfile );
		Sys_FileClose( packhandle );
		return NULL;
	}
	if ( numpackfiles > MAX_FILES_IN_PACK ) {
		Sys_Error( "%s has %i files, more than %i max.", packfile, numpackfiles, MAX_FILES_IN_PACK );
	}
	if ( numpackfiles != PAK0_COUNT ) {
		/* TODO: change this for librequake */
		com_modified = true; /* Not the original file */
	}

	newfiles = (packfile_t *) Z_Malloc( numpackfiles * sizeof( packfile_t) );

	Sys_FileSeek( packhandle, header.dirofs );
	if ( Sys_FileRead( packhandle, info, header.dirlen ) != header.dirlen ) {
		Sys_Error( "Error reading %s", packfile );
	}

	/* CRC the directory to check for modifications */
	/* TODO: change for librequake */
	CRC_Init( &crc );
	for ( ii = 0; ii < header.dirlen; ii++ ) {
		CRC_ProcessByte( &crc, ( (byte *)info )[ii] );
	}
	if ( crc != PAK_CRC_V106 && crc != PAK_CRC_V101 && crc != PAK_CRC_V100 )
		com_modified = true;

	/* Parse the directory */
	for ( ii = 0; ii < numpackfiles; ii++ ) {
		q_strlcpy( newfiles[ii].name, info[ii].name, sizeof( newfiles[ii].name ) );
		newfiles[ii].filepos = LittleLong( info[ii].filepos );
		newfiles[ii].filelen = LittleLong( info[ii].filelen );
	}

	pack = (pack_t *) Z_Malloc( sizeof( pack_t ) );
	q_strlcpy( pack->filename, packfile, sizeof( pack->filename ) );
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	Sys_Printf( "Added packfile %s (%i files)\n", packfile, numpackfiles );
	return pack;
}

/*
===============
COM_AddGameDirectory -- johnfitz -- modified based on topaz's tutorial
===============
*/
static void COM_AddGameDirectory( const char *base, const char *dir ) {
	int		ii;
	unsigned int	path_id;
	searchpath_t	*search;
	pack_t		*pack, qspack;
	char		packfile[MAX_OSPATH];
	qboolean	been_here = false;

	q_strlcpy( com_gamedir, va( "%s%s", base, dir ), sizeof( com_gamedir ) );

	/* Assign a path_id to this game directory */
	if ( com_searchpaths ) {
		path_id = com_searchpaths->path_id << 1;
	}
	else {
		path_id = 1U;
	}

_add_path:
	/* Add the directory to the search path */
	search = (searchpath_t *) Z_Malloc( sizeof( searchpath_t ) );
	search->path_id = path_id;
	q_strlcpy( search->filename, com_gamedir, sizeof( search->filename ) );
	search->next = com_searchpaths;
	com_searchpaths = search;

	/* Add any pack files in the format pak0.pak, pak1.pak, ... */
	for ( ii = 0; ; ii++ ) {
		q_snprintf( packfile, sizeof( packfile ), "%s/pak%i.pak", com_gamedir, ii );
		pack = COM_LoadPackFile( packfile );
		if ( ii != 0 || path_id != 1 || fitzmode ) {
			qspack = NULL;
		}
		else {
			qboolean old = com_modified;
			if ( been_here ) {
				base = host_parms->userdir;
			}
			q_snprintf( packfile, sizeof( packfile ), "%s/quakespasm.pak", base );
			qspack = COM_LoadPackFile( packfile );
			com_modified = old;
		}
		if ( pack ) {
			search = ( searchpath_t *) Z_Malloc( sizeof( sizeof( searchpath_t ) ) );
			search->path_id = path_id;
			search->pack = pack;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
		if ( qspack ) {
			search = ( searchpath_t *) Z_Malloc( sizeof( searchpath_t ) );
			search->path_id = path_id;
			search->pack = qspack;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
		if ( !pack ) {
			break;
		}
	}
	if ( !been_here && host_params->userdir != host_parms->basedir ) {
		been_here = true;
		q_strlcpy( com_gamedir, va( "%s/%s", host_parms->userdir, dir ), sizeof( com_gamedir ) );
		Sys_mkdir( com_gamedir );
		goto _add_path;
	}
}

/*
================================================================================
johnfitz -- dynamic gamedir stuff - modified by QuakeSpasm team.
================================================================================
*/
static void COM_Game_f( void ) {
	if ( Cmd_Argc( ) > 1 ) {
		const char *p1 = Cmd_Argv( 1 );
		const char *p2 = Cmd_Argv( 2 );
		searchpath_t *search;

		if ( !registered.value ) {
			Con_Printf( "You must have the registered version to use modified games.\n" );
			return;
		}
		if ( !*p1 ||
				!strcmp( p1, "." ) ||
			 	strstr( p1, ".." ) ||
			 	strstr( p1, "/" ) ||
			 	strstr( p1, "\\" ) ||
			 	strstr( p1, ":" ) ) {
			Con_Printf( "gamedir should be a single directory name, not a path\n" );
			return;
		}

		if ( !*p2 ) {
			if ( strcmp( p2, "-hipnotic" ) && strcmp( p2, "-rogue" ) && strcmp( p2, "-quoth" ) ) {
				Con_Printf( "Invalid mission pack argument to \"game\".\n" );
				return;
			}
			if ( !q_strcasecmp( p1, GAMENAME ) ) {
				Con_Printf( "No mission pack arguments to %s game.\n", GAMENAME );
				return;
			}
		}

		if ( !q_strcasecmp( p1, COM_SkipPath( com_gamedir ) ) ) { /* No Change */
			if ( com_searchpaths->path_id > 1 ) { /* Current game not id1 */
				if ( *p2 && com_searchpaths->path_id == 2 ) {
					/*
					 * rely on Quakespasm extension treating "-game missionpack"
					 * as "-missionpack", otherwise would be a mess
					 */
					if ( !q_strcasecmp( p1, &p2[1] ) ) {
						goto _same;
					}
					Con_Printf( "Reloading game \"%s\" with \"%s\" support", p1, &p2[1] );
				}
				else if ( !*p2 && com_searchpaths->path_id > 2 ) {
					Con_Printf( "Reloading game \"%s\" without mission pack support\n", p1 );
				}
				else {
					goto _same;
				}
			}
			else {
			_same:
				Con_Printf( "\"game\" is already \"%s\"\n", COM_SkipPath( com_gamedir ) );
				return;
			}
		}

		com_modified = true;

		/* End the Server */
		CL_Disconnect( );
		Host_ShutdownServer( true );

		/* Write config file */
		Host_WriteConfiguration( );

		/* End the extra game if it is loaded */
		while ( com_searchpaths != com_base_searchpaths ) {
			if ( com_searchpaths->paths ) {
				Sys_FileClose( com_searchpaths->pack->files );
				Z_Free( com_searchpaths->pack->files );
				Z_Free( com_searchpaths->pack );
			}
			search = com_searchpaths->next;
			Z_Free( com_searchpaths );
			com_searchpaths = search;
		}
		hipnotic = false;
		rogue = false;
		standard_quake = true;

		if ( q_strcasecmp( p1, GAMENAME ) ) { /* game is not id1 */
			if ( *p2 ) {
				COM_AddGameDirectory( com_basedir, &p2[1] );
				standard_quake = false;
				if ( !strcmp( p2, "-hipnotic" ) || !strcmp( p2, "-quoth" ) ) {
					hipnotic = true;
				}
				else if ( !strcmp( p2, "-rogue" ) ) {
					rogue = true;
				}
				if ( q_strcasecmp( p1, &p2[1]) ) { /* Don't load twice */
					COM_AddGameDirectory( com_basedir, p1 );
				}
			}
			else {
				COM_AddGameDirectory( com_basedir, p1 );
				/* Quakespasm extension: treat "-game missionpack" as "-missionpack" */
				if ( !q_strcasecmp( p1, "hipnotic" ) || !q_strcasecmp( p1, "quoth" ) ) {
					hipnotic = true;
					standard_quake = false;
				}
				else if ( !q_strcasecmp( p1, "rogue" ) ) {
					rogue = true;
					standard_quake = false;
				}
			}
		}
		else { /* Just update com_gamedir */
			
		}
	}
}
































