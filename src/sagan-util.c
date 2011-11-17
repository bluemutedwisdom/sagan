/*
** Copyright (C) 2009-2011 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2011 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* sagan-util.c 
 *
 * Various re-usable functions. 
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#ifdef HAVE_LIBMYSQLCLIENT_R
#include <mysql/mysql.h>
MYSQL    *mysql;
#endif

#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>

#include "sagan.h"

#include "version.h"

sbool daemonize;

/*****************************************************************************
 * This force Sagan to chroot.                                               *
 *                                                                           *
 * Note: printf/fprints are used,  because we actually chroot before the log *
 * it initalized                                                             *
 *****************************************************************************/

void sagan_chroot(const char *username, const char *chrootdir ) { 

struct passwd *pw = NULL;

pw = getpwnam(username);

printf("[*] Chroot to %s\n", chrootdir);

if (chroot(chrootdir) != 0 || chdir ("/") != 0) {
    fprintf(stderr, "[E] Could not chroot to '%s'.\n",  chrootdir);
    exit(1);		/* sagan.log isn't open yet */
   }
}

/************************************************
 * Drop priv's so we aren't running as "root".  *
 ************************************************/

void sagan_droppriv(_SaganConfig *config, const char *username)
{

	struct stat fifocheck;
        struct passwd *pw = NULL;
	int ret;

        pw = getpwnam(username);

	if (!pw) sagan_log(config, 1, "Couldn't locate user '%s'. Aborting...", username);
        
	if ( getuid() == 0 ) {
	sagan_log(config, 0, "Dropping privileges [UID: %lu GID: %lu]", (unsigned long)pw->pw_uid, (unsigned long)pw->pw_gid);
	ret = chown(config->sagan_fifo, (unsigned long)pw->pw_uid,(unsigned long)pw->pw_gid);

	        if (stat(config->sagan_fifo, &fifocheck) != 0 ) sagan_log(config, 1, "Cannot open %s FIFO!", config->sagan_fifo);

		if ( ret < 0 ) sagan_log(config, 1, "[%s, line %d] Cannot change ownership of %s to username %s", __FILE__, __LINE__, config->sagan_fifo, username);

                if (initgroups(pw->pw_name, pw->pw_gid) != 0 ||
                    setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
		    sagan_log(config, 1, "Could not drop privileges to uid: %lu gid: %lu!", (unsigned long)pw->pw_uid, (unsigned long)pw->pw_gid);
	       } 
	       
	       } else { 
	       sagan_log(config, 0, "Not dropping privileges.  Already running as a non-privileged user");
	       }
}

/***************************************************************/
/* Convert syslog data to hex for input into the payload table */
/***************************************************************/

char *fasthex(char *xdata, int length)
{
    char conv[] = "0123456789ABCDEF";
    char *retbuf = NULL;
    char *index;
    char *end;
    char *ridx;

    index = xdata;
    end = xdata + length;
    retbuf = (char *) calloc((length*2)+1, sizeof(char));
    ridx = retbuf;

    while(index < end)
    {
        *ridx++ = conv[((*index & 0xFF)>>4)];
        *ridx++ = conv[((*index & 0xFF)&0x0F)];
        index++;
    }

    return(retbuf);
}

/* Removes quotes from msg, pcre, etc */

char  *remquotes(char *s) {
       char *s1, *s2;
       for(s1 = s2 = s;*s1;*s1++ = *s2++ )
       while( *s2 == '"' )s2++;
       return s;
}

char  *remrt(char *s) {
       char *s1, *s2;
       for(s1 = s2 = s;*s1;*s1++ = *s2++ )
       while( *s2 == '\n' )s2++;
      return s;
}


/* Removes spaces from certain rule fields, etc */

char *remspaces(char *s) {
       char *s1, *s2;
       for(s1 = s2 = s;*s1;*s1++ = *s2++ )
       while( *s2 == ' ')s2++;
       return s;
}

char *toupperc(char* const s) {
        char* cur = s;
          while (*cur) {
          *cur = toupper(*cur);
          ++cur;
          }
  return s;
}


void sagan_log (_SaganConfig *config , int type, const char *format,... ) {

   char buf[1024];
   va_list ap;
   va_start(ap, format);
   char *chr="*";
   char curtime[64];
   time_t t;
   struct tm *now;
   t = time(NULL);
   now=localtime(&t);
   strftime(curtime, sizeof(curtime), "%m/%d/%Y %H:%M:%S",  now);

   if ( type == 1 ) chr="E";

     vsnprintf(buf, sizeof(buf), format, ap);
     fprintf(config->sagan_log_stream, "[%s] [%s] - %s\n", chr, curtime, buf);
     fflush(config->sagan_log_stream);

     if ( daemonize == 0) printf("[%s] %s\n", chr, buf);
     if ( type == 1 ) exit(1);
}

int checkendian() {
   int i = 1;
   char *p = (char *) &i;
        if (p[0] == 1) // Lowest address contains the least significant byte
        return 0; // Little endian
        else
        return 1; // Big endian
}


/* Converts IP address.  For IPv4,  we convert the quad IP string to a 32 bit
 * value.  We return the unsigned long value as a pointer to a string because
 * that's the way IPv6 is done.  Basically,  we'll probably want IPv6 when 
 * snort supports DB IPv6.
 */

int ip2bit ( _SaganConfig *config, char *ipaddr ) { 

struct sockaddr_in ipv4;
uint32_t ip;

/* Change to AF_UNSPEC for future ipv6 */
/* Champ Clark III - 01/18/2011 */

if (!inet_pton(AF_INET, ipaddr, &ipv4.sin_addr)) {
sagan_log(config, 0, "Warning: inet_pton() error,  but continuing...");
}

if ( config->endian == 0 ) {
   ip = htonl(ipv4.sin_addr.s_addr);
   } else {
   ip = ipv4.sin_addr.s_addr;
   }

return(ip);
}

int isnumeric (char *str) {

if(strlen(str) == strspn(str, "0123456789")) {
	return(1);
	 } else {
	return(0);
	}
}

/* Escape SQL.   This was taken from Prelude.  */

#if defined(HAVE_LIBMYSQLCLIENT_R) || defined(HAVE_LIBPQ)
char *sql_escape(_SaganConfig *config, const char *string, int from )
{
        size_t len;
        char *escaped=NULL;
	char *escapedre=NULL;
	char tmpescaped[MAX_SYSLOGMSG];


        if ( ! string )
                return strdup("NULL");
        /*
         * MySQL documentation say :
         * The string pointed to by from must be length bytes long. You must
         * allocate the to buffer to be at least length*2+1 bytes long. (In the
         * worse case, each character may need to be encoded as using two bytes,
         * and you need room for the terminating null byte.)
         */
        len = strlen(string);

        escaped = malloc(len * 2 + 3);
        
	if (! escaped) {
                sagan_log(config, 1, "[%s, line %d] Memory exhausted.", __FILE__, __LINE__ );
                return NULL;
        }

        escaped[0] = '\'';

/* Snort */
if ( from == 0 ) { 
#ifdef HAVE_LIBMYSQLCLIENT_R
#if MYSQL_VERSION_ID >= 32200
        len = mysql_real_escape_string(mysql, escaped + 1, string, len);
#else
        len = mysql_escape_string(escaped + 1, string, len);
#endif
#endif

        escaped[len + 1] = '\'';
        escaped[len + 2] = '\0';
}

	/* Temp. copy value,  and free(escaped) to prevent mem. leak */

	snprintf(tmpescaped, sizeof(tmpescaped), "%s", escaped);
	escapedre=tmpescaped;
	free(escaped);

	return(escapedre);
}
#endif

/* Grab's information between "quotes" and returns it.  Use for things like
 * parsing msg: and pcre */

char *betweenquotes(char *instring)
{
sbool flag=0;
int i;
char tmp1[2];

/* quick and dirty fix added by drforbin....this function really should be reworked 
fix added to make tmp2 presistent (non-automatic) so once the function returns it is presistent */

static char tmp2[512];
memset(tmp2,0,sizeof(tmp2));
char *ret;

for ( i=0; i<strlen(instring); i++) { 

if ( flag == 1 && instring[i] == '\"' ) flag = 0;
if ( flag == 1 ) { 
   snprintf(tmp1, sizeof(tmp1), "%c", instring[i]); 
   strlcat(tmp2, tmp1, sizeof(tmp2));
   }

if ( instring[i] == '\"' ) flag++;

}

ret=tmp2;
return(ret);
}

/* CalcPct (Taken from Snort) */

double CalcPct(uint64_t cnt, uint64_t total)
{
    double pct = 0.0;

    if (total == 0.0)
    {
        pct = (double)cnt;
    }
    else
    {
        pct = (double)cnt / (double)total;
    }

    pct *= 100.0;

    return pct;
}

/* DNS lookup of hostnames.  Wired for IPv4 and IPv6.  Code largely
 * based on Beej's showip.c */

char *dns_lookup( _SaganConfig *config, char *host) 
{
    struct addrinfo hints, *res; //,// *p;
    int status;
    char ipstr[INET6_ADDRSTRLEN];
    char *ret;
    void *addr;

       if ( config->disable_dns_warnings == 0 ) { 
       sagan_log(config, 0, "--------------------------------------------------------------------------");
       sagan_log(config, 0, "Sagan DNS lookup need for %s.", host); 
       sagan_log(config, 0, "This can affect performance.  Please see:" );
       sagan_log(config, 0, "https://wiki.quadrantsec.com/bin/view/Main/SaganDNS");
       sagan_log(config, 0, "--------------------------------------------------------------------------");
       }

       memset(&hints, 0, sizeof hints);
       hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
       hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(host, NULL, &hints, &res)) != 0) {
	sagan_log(config, 0, "getaddrinfo: %s", gai_strerror(status));
        return NULL;
    }

        if (res->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
            addr = &(ipv4->sin_addr);
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)res->ai_addr;
            addr = &(ipv6->sin6_addr);
        }
     
    inet_ntop(res->ai_family, addr, ipstr, sizeof ipstr);
    ret=ipstr;
    return ret;
}


/* String replacement function.  Used for things like $RULE_PATH */

char *sagan_replace_str(char *str, char *orig, char *rep)
{

  static char buffer[4096];
  char *p;

  if(!(p = strstr(str, orig)))  return str;

  strlcpy(buffer, str, p-str); 
  buffer[p-str] = '\0';
  sprintf(buffer+(p-str), "%s%s", rep, p+strlen(orig));
  return(buffer);
}


/* Get the filename from a path */

char *sagan_getfilename(char *file) {

    char *pfile;
    pfile = file + strlen(file);
    for (; pfile > file; pfile--)
    {
        if ((*pfile == '\\') || (*pfile == '/'))	/* *nix/Windows */
        {
            pfile++;
            break;
        }
    }

return(pfile);

}

