/* 
Serval Distributed Numbering Architecture (DNA)
Copyright (C) 2010 Paul Gardner-Stephen 

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "serval.h"
#include "strbuf.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

unsigned int debug = 0;

static FILE *logfile = NULL;
static int flag_show_pid = -1;
static int flag_show_time = -1;

/* The logbuf is used to accumulate log messages before the log file is open and ready for
   writing.
 */
static char _log_buf[8192];
static struct strbuf logbuf = STRUCT_STRBUF_EMPTY;

#ifdef ANDROID
#include <android/log.h>
#endif

void set_logging(FILE *f)
{
  logfile = f;
  if (f == stdout)
    INFO("Logging to stdout");
  else if (f == stderr)
    INFO("Logging to stderr");
  else if (f != NULL)
    INFOF("Logging to stream with fd=%d", fileno(f));
}

FILE *open_logging()
{
  if (!logfile) {
    const char *logpath = getenv("SERVALD_LOG_FILE");
    if (!logpath) {
      // If the configuration is locked (eg, it called WHY() or DEBUG() while initialising, which
      // led back to here) then return NULL to indicate the message cannot be logged.
      if (confLocked())
	return NULL;
      logpath = confValueGet("log.file", NULL);
    }
    if (!logpath) {
      logfile = stderr;
      INFO("No logfile configured -- logging to stderr");
    } else if ((logfile = fopen(logpath, "a"))) {
      setlinebuf(logfile);
      INFOF("Logging to %s (fd %d)", logpath, fileno(logfile));
    } else {
      logfile = stderr;
      WARN_perror("fopen");
      WARNF("Cannot append to %s -- falling back to stderr", logpath);
    }
  }
  return logfile;
}

static int show_pid()
{
  if (flag_show_pid < 0 && !confLocked())
    flag_show_pid = confValueGetBoolean("log.show_pid", 0);
  return flag_show_pid;
}

static int show_time()
{
  if (flag_show_time < 0 && !confLocked())
    flag_show_time = confValueGetBoolean("log.show_time", 0);
  return flag_show_time;
}

void close_logging()
{
  if (logfile) {
    fclose(logfile);
    logfile = NULL;
  }
}

void logMessage(int level, const char *file, unsigned int line, const char *function, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vlogMessage(level, file, line, function, fmt, ap);
  va_end(ap);
}

void vlogMessage(int level, const char *file, unsigned int line, const char *function, const char *fmt, va_list ap)
{
  if (level != LOG_LEVEL_SILENT) {
    if (strbuf_is_empty(&logbuf))
      strbuf_init(&logbuf, _log_buf, sizeof _log_buf);
#ifndef ANDROID
    const char *levelstr = "UNKWN:";
    switch (level) {
      case LOG_LEVEL_FATAL: levelstr = "FATAL:"; break;
      case LOG_LEVEL_ERROR: levelstr = "ERROR:"; break;
      case LOG_LEVEL_INFO:  levelstr = "INFO:"; break;
      case LOG_LEVEL_WARN:  levelstr = "WARN:"; break;
      case LOG_LEVEL_DEBUG: levelstr = "DEBUG:"; break;
    }
    strbuf_sprintf(&logbuf, "%-6s ", levelstr);
#endif
    if (show_pid())
      strbuf_sprintf(&logbuf, "[%5u] ", getpid());
    if (show_time()) {
      struct timeval tv;
      if (gettimeofday(&tv, NULL) == -1) {
	strbuf_puts(&logbuf, "NOTIME______");
      } else {
	struct tm tm;
	char buf[20];
	if (strftime(buf, sizeof buf, "%T", localtime_r(&tv.tv_sec, &tm)) == 0)
	  strbuf_puts(&logbuf, "EMPTYTIME___");
	else
	  strbuf_sprintf(&logbuf, "%s.%03u ", buf, tv.tv_usec / 1000);
      }
    }
    strbuf_sprintf(&logbuf, "%s:%u:%s()  ", file ? trimbuildpath(file) : "NULL", line, function ? function : "NULL");
    strbuf_vsprintf(&logbuf, fmt, ap);
    strbuf_puts(&logbuf, "\n");
#ifdef ANDROID
    int alevel = ANDROID_LOG_UNKNOWN;
    switch (level) {
      case LOG_LEVEL_FATAL: alevel = ANDROID_LOG_FATAL; break;
      case LOG_LEVEL_ERROR: alevel = ANDROID_LOG_ERROR; break;
      case LOG_LEVEL_INFO:  alevel = ANDROID_LOG_INFO; break;
      case LOG_LEVEL_WARN:  alevel = ANDROID_LOG_WARN; break;
      case LOG_LEVEL_DEBUG: alevel = ANDROID_LOG_DEBUG; break;
    }
    __android_log_print(alevel, "servald", "%s", strbuf_str(&logbuf));
    strbuf_reset(&logbuf);
#else
    FILE *logf = open_logging();
    if (logf) {
      fputs(strbuf_str(&logbuf), logf);
      if (strbuf_overrun(&logbuf))
	fputs("OVERRUN\n", logf);
      strbuf_reset(&logbuf);
    }
#endif
  }
}

const char *trimbuildpath(const char *path)
{
  /* Remove common path prefix */
  int lastsep = 0;
  int i;
  for (i = 0; __FILE__[i] && path[i]; ++i) {
    if (i && path[i - 1] == '/')
      lastsep = i;
    if (__FILE__[i] != path[i])
      break;
  }
  return &path[lastsep];
}

int dump(char *name, unsigned char *addr, size_t len)
{
  char buf[100];
  size_t i;
  DEBUGF("Dump of %s", name);
  for(i = 0; i < len; i += 16) {
    strbuf b = strbuf_local(buf, sizeof buf);
    strbuf_sprintf(b, "  %04x :", i);
    int j;
    for (j = 0; j < 16 && i + j < len; j++)
      strbuf_sprintf(b, " %02x", addr[i + j]);
    for (; j < 16; j++)
      strbuf_puts(b, "   ");
    strbuf_puts(b, "    ");
    for (j = 0; j < 16 && i + j < len; j++)
      strbuf_sprintf(b, "%c", addr[i+j] >= ' ' && addr[i+j] < 0x7f ? addr[i+j] : '.');
    DEBUG(strbuf_str(b));
  }
  return 0;
}

char *catv(const char *data, char *buf, size_t len)
{
  strbuf b = strbuf_local(buf, len);
  for (; *data && !strbuf_overrun(b); ++data) {
    if (*data == '\n') strbuf_puts(b, "\\n");
    else if (*data == '\r')   strbuf_puts(b, "\\r");
    else if (*data == '\t')   strbuf_puts(b, "\\t");
    else if (*data == '\\')   strbuf_puts(b, "\\\\");
    else if (isprint(*data))  strbuf_putc(b, *data);
    else		      strbuf_sprintf(b, "\\x%02x", *data);
  }
  return buf;
}

int dumpResponses(struct response_set *responses)
{
  struct response *r;
  if (!responses) {
    DEBUG("Response set is NULL");
    return 0;
  }
  DEBUGF("Response set claims to contain %d entries.", responses->response_count);
  r = responses->responses;
  while(r) {
    DEBUGF("  response code 0x%02x", r->code);
    if (r->next && r->next->prev != r)
      DEBUG("    !! response chain is broken");
    r = r->next;
  }
  return 0;
}

unsigned int debugFlagMask(const char *flagname) {
  if	  (!strcasecmp(flagname,"all"))			return DEBUG_ALL;
  else if (!strcasecmp(flagname,"interfaces"))		return DEBUG_OVERLAYINTERFACES;
  else if (!strcasecmp(flagname,"rx"))			return DEBUG_PACKETRX;
  else if (!strcasecmp(flagname,"tx"))			return DEBUG_PACKETTX;
  else if (!strcasecmp(flagname,"verbose"))		return DEBUG_VERBOSE;
  else if (!strcasecmp(flagname,"verbio"))		return DEBUG_VERBOSE_IO;
  else if (!strcasecmp(flagname,"peers"))		return DEBUG_PEERS;
  else if (!strcasecmp(flagname,"dnaresponses"))	return DEBUG_DNARESPONSES;
  else if (!strcasecmp(flagname,"dnahelper"))		return DEBUG_DNAHELPER;
  else if (!strcasecmp(flagname,"simulation"))		return DEBUG_SIMULATION;
  else if (!strcasecmp(flagname,"packetformats"))	return DEBUG_PACKETFORMATS;
  else if (!strcasecmp(flagname,"packetconstruction"))	return DEBUG_PACKETCONSTRUCTION;
  else if (!strcasecmp(flagname,"gateway"))		return DEBUG_GATEWAY;
  else if (!strcasecmp(flagname,"hlr"))			return DEBUG_HLR;
  else if (!strcasecmp(flagname,"sockio"))		return DEBUG_IO;
  else if (!strcasecmp(flagname,"frames"))		return DEBUG_OVERLAYFRAMES;
  else if (!strcasecmp(flagname,"abbreviations"))	return DEBUG_OVERLAYABBREVIATIONS;
  else if (!strcasecmp(flagname,"routing"))		return DEBUG_OVERLAYROUTING;
  else if (!strcasecmp(flagname,"security"))		return DEBUG_SECURITY;
  else if (!strcasecmp(flagname,"rhizome"))	        return DEBUG_RHIZOME;
  else if (!strcasecmp(flagname,"rhizometx"))		return DEBUG_RHIZOME_TX;
  else if (!strcasecmp(flagname,"rhizomerx"))		return DEBUG_RHIZOME_RX;
  else if (!strcasecmp(flagname,"monitorroutes"))	return DEBUG_OVERLAYROUTEMONITOR;
  else if (!strcasecmp(flagname,"queues"))		return DEBUG_QUEUES;
  else if (!strcasecmp(flagname,"broadcasts"))		return DEBUG_BROADCASTS;
  else if (!strcasecmp(flagname,"manifests"))		return DEBUG_MANIFESTS;
  else if (!strcasecmp(flagname,"mdprequests"))		return DEBUG_MDPREQUESTS;
  else if (!strcasecmp(flagname,"timing"))		return DEBUG_TIMING;
  return 0;
}

static strbuf _toprint(strbuf sb, const char *srcBuf, size_t srcBytes)
{
  strbuf_putc(sb, '"');
  for (; srcBytes && !strbuf_overrun(sb); ++srcBuf, --srcBytes) {
    if (*srcBuf == '\0')
      strbuf_puts(sb, "\\0");
    else if (*srcBuf == '\n')
      strbuf_puts(sb, "\\n");
    else if (*srcBuf == '\r')
      strbuf_puts(sb, "\\r");
    else if (*srcBuf == '\t')
      strbuf_puts(sb, "\\t");
    else if (*srcBuf == '\\')
      strbuf_puts(sb, "\\\\");
    else if (*srcBuf >= ' ' && *srcBuf <= '~')
      strbuf_putc(sb, *srcBuf);
    else
      strbuf_sprintf(sb, "\\x%02x", *srcBuf);
  }
  strbuf_putc(sb, '"');
  if (strbuf_overrun(sb)) {
    strbuf_trunc(sb, -4);
    strbuf_puts(sb, "\"...");
  }
  return sb;
}

/* Format a buffer of data as a printable representation, eg: "Abc\x0b\n\0", for display
   in log messages.  If dstStrLen == -1 then assumes the dstStr buffer is large enough to
   hold the representation of the entire srcBuf.
   @author Andrew Bettison <andrew@servalproject.com>
 */
char *toprint(char *dstStr, ssize_t dstStrLen, const char *srcBuf, size_t srcBytes)
{
  return strbuf_str(_toprint(strbuf_local(dstStr, (dstStrLen == -1 ? 2 + srcBytes * 4 : dstStrLen) + 1), srcBuf, srcBytes));
}

/* Compute the length of the printable string produced by toprint().  If dstStrLen == -1 then
   returns the exact number of characters in the printable representation, otherwise returns
   dstStrLen.
   @author Andrew Bettison <andrew@servalproject.com>
 */
size_t toprint_strlen(ssize_t dstStrLen, const char *srcBuf, size_t srcBytes)
{
  return dstStrLen == -1 ? strbuf_count(_toprint(strbuf_local(NULL, 0), srcBuf, srcBytes)) : dstStrLen;
}

/* Read the symbolic link into the supplied buffer and add a terminating nul.  Return -1 if the
 * buffer is too short to hold the link content and the nul.  If readlink(2) returns an error, then
 * logs it and returns -1.  Otherwise, returns the number of bytes read, including the terminating
 * nul, ie, returns what readlink(2) returns plus one.  If the 'len' argument is given as zero, then
 * returns the number of bytes that would be read, by calling lstat(2) instead of readlink(2), plus
 * one for the terminating nul.  Beware of the following race condition: a symbolic link may be
 * altered between calling the lstat(2) and readlink(2), so the following apparently overflow-proof
 * code may still fail from a buffer overflow in the second call to read_symlink():
 *
 *    char *readlink_malloc(const char *path) {
 *	ssize_t len = read_symlink(path, NULL, 0);
 *	if (len == -1)
 *	  return NULL;
 *	char *buf = malloc(len);
 *	if (buf == NULL)
 *	  return NULL;
 *	if (read_symlink(path, buf, len) == -1) {
 *	  free(buf);
 *	  return NULL;
 *	}
 *	return buf;
 *    }
 *
 * @author Andrew Bettison <andrew@servalproject.com>
 */
ssize_t read_symlink(const char *path, char *buf, size_t len)
{
  if (len == 0) {
    struct stat stat;
    if (lstat(path, &stat) == -1)
      return WHYF_perror("lstat(%s)", path);
    return stat.st_size;
  }
  ssize_t nr = readlink(path, buf, len);
  if (nr == -1)
    return WHYF_perror("readlink(%s)", path);
  if (nr >= len)
    return WHYF("buffer overrun from readlink(%s, len=%lu)", path, (unsigned long) len);
  buf[nr] = '\0';
  return nr;
}

ssize_t get_self_executable_path(char *buf, size_t len)
{
#ifdef linux
  return read_symlink("/proc/self/exe", buf, len);
#endif
#ifdef __APPLE__
  // Mac OS X
  // TODO: Not tested
  uint32_t bufsize = len;
  return _NSGetExecutablePath(buf, &bufsize) == -1 && len ? -1 : bufsize;
#endif
  return WHYF("Not implemented");
}

int log_backtrace()
{
  open_logging();
  char execpath[160];
  if (get_self_executable_path(execpath, sizeof execpath) == -1)
    return WHY("cannot log backtrace: own executable path unknown");
  char tempfile[] = "/tmp/servalXXXXXX.gdb";
  int tmpfd = mkstemps(tempfile, 4);
  if (tmpfd == -1)
    return WHY_perror("mkstemps");
  if (write_str(tmpfd, "backtrace\n") == -1) {
    close(tmpfd);
    unlink(tempfile);
    return -1;
  }
  if (close(tmpfd) == -1) {
    WHY_perror("close");
    unlink(tempfile);
    return -1;
  }
  char pidstr[12];
  snprintf(pidstr, sizeof pidstr, "%u", getpid());
  int stdout_fds[2];
  if (pipe(stdout_fds) == -1)
    return WHY_perror("pipe");
  pid_t child_pid;
  switch (child_pid = fork()) {
  case -1: // error
    WHY_perror("fork");
    close(stdout_fds[0]);
    close(stdout_fds[1]);
    return WHY("cannot log backtrace: fork failed");
  case 0: // child
    if (dup2(stdout_fds[1], 1) == -1)
      _exit(-1);
    close(0);
    if (open("/dev/null", O_RDONLY) != 0)
      _exit(-2);
    close(stdout_fds[0]);
    close(2);
    execlp("gdb", "gdb", "-n", "-batch", "-x", tempfile, execpath, pidstr, NULL);
    do { _exit(-3); } while (1);
    break;
  }
  // parent
  close(stdout_fds[1]);
  char buf[16384];
  size_t len = 0;
  ssize_t nr;
  while (len < sizeof buf - 1 && (nr = read(stdout_fds[0], buf + len, sizeof buf - 1 - len)) > 0)
    len += nr;
  buf[len] = '\0';
  if (nr == -1)
    WHY_perror("read");
  close(stdout_fds[0]);
  int status = 0;
  if (waitpid(child_pid, &status, 0) == -1)
    WHY_perror("waitpid");
  DEBUGF("gdb backtrace status=0x%x\n%s", status, buf);
  unlink(tempfile);
  return 0;
}
