/* Copyright 2008-2024 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
 *
 * This file is part of smrender.
 *
 * Smrender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smrender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file smlog.c
 * This file simply contains the logging functions. It was originally written
 * for OnionCat and was adapted to be used for smrender.
 * 
 *  @author Bernhard R. Fischer
 *  @version 2024/05/13
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif

#include "smrender.h"

// Define the following macro if log_msg() should preserve errno.
#define PRESERVE_ERRNO

#define SIZE_1K 1024
#define TIMESTRLEN 64
#define CBUFLEN SIZE_1K

#ifndef LOG_PRI
#define LOG_PRI(p) ((p) & LOG_PRIMASK)
#endif

int sm_thread_id(void);

static const char *flty_[8] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
enum {ANSI_BLACK, ANSI_RED, ANSI_GREEN, ANSI_YELLOW, ANSI_BLUE, ANSI_MAGENTA, ANSI_CYAN, ANSI_WHITE, ANSI_RESET};
static const char *acol_[] = {"\x1b[30m", "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[34m",  "\x1b[35m", "\x1b[36m", "\x1b[37m", "\x1b[0m"};
//! FILE pointer to log
static FILE *log_ = NULL;
static int level_ = LOG_INFO;
static int flags_ = LOGF_TIME;


void __attribute__((constructor)) init_log0(void)
{
   log_ = stderr; 
   (void) sm_thread_id();
}


void set_log_flags(int f)
{
   // do not set LOGF_COLOR if output is sent to file
   if (log_ != stderr && f == LOGF_COLOR)
      return;

   flags_ |= f;
}


void clear_log_flags(int f)
{
   flags_ &= ~f;
}


int test_flag(int f)
{
   return flags_ & f;
}


/*! Enable (a != 0) or disable (a == 0) logging of timestamp. By default
 * timestamp logging is enabled.
 * @param a Set to true to enable timestamp logging or false (a == 0) to
 * disable it.
 */
void set_log_time(int a)
{
   if (a)
      set_log_flags(LOGF_TIME);
   else
      clear_log_flags(LOGF_TIME);
}


/*! Init logging.
 * @param s Name of logfile. If s == 'stderr' it will log to stderr instead if
 * a file. If the string is prepended by a '+' the logging will append to the
 * logfile if it exists already. Otherwise the file will be truncated.
 * @param level Loglevel according to syslog(3).
 * @return The function returns a FILE pointer to the logfile.
 */
FILE *init_log(const char *s, int level)
{
   const char *mode = "w";
   level_ = level;

   if (s == NULL || !strcmp(s, "stderr"))
      log_ = stderr;
   else
   {
      if (*s == '+')
      {
         s++;
         mode = "a";
      }

      if ((log_ = fopen(s, mode)) == NULL)
         fprintf(stderr, "*** could not open logfile %s: %s. Logging to syslog.\n", s, strerror(errno));

      int e;
      if ((e = setvbuf(log_, NULL, _IOLBF, 0)))
         fprintf(stderr, "*** setvbuf() returned %d: %s", e, strerror(errno));

      // do not use ANSI colors in file
      clear_log_flags(LOGF_COLOR);
   }

   return log_;
}


static int level_color(int level)
{
   switch (level)
   {
      case LOG_DEBUG:
         return ANSI_MAGENTA;

      case LOG_INFO:
      case LOG_NOTICE:
         return ANSI_GREEN;

      case LOG_WARNING:
         return ANSI_YELLOW;

      default:
         return ANSI_RED;
   }
}


/*! Log a message to a file or syslogd.
 *  @param out Open FILE pointer or NULL. In the latter case it will log to
 *  syslogd.
 *  @param lf Logging priority (equal to syslog)
 *  @param fmt Format string
 *  @param ap Variable parameter list
 *  @return Returns the number of bytes effectively written.
 */
int vlog_msgf(FILE *out, int lf, const char *fmt, va_list ap)
{
#ifdef WITH_THREADS
   static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
   static struct timeval tv_stat = {0, 0};
   struct timeval tv, tr;
   struct tm *tm;
   time_t t;
   char timestr[TIMESTRLEN] = "", timez[TIMESTRLEN] = "";
   int level = LOG_PRI(lf);
   char buf[SIZE_1K];
   int len;

   if (level_ < level) return 0;

   //t = time(NULL);
   if (gettimeofday(&tv, NULL) == -1)
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, strerror(errno)), exit(EXIT_FAILURE);

   if (!tv_stat.tv_sec) tv_stat = tv;

   tr.tv_sec = tv.tv_sec - tv_stat.tv_sec;
   tr.tv_usec = tv.tv_usec - tv_stat.tv_usec;
   if (tr.tv_usec < 0)
   {
      tr.tv_usec += 1000000;
      tr.tv_sec--;
   }

   t = tv.tv_sec;
   if ((tm = localtime(&t)))
   {
      //(void) strftime(timestr, TIMESTRLEN, "%a, %d %b %Y %H:%M:%S", tm);
      (void) strftime(timestr, TIMESTRLEN, "%H:%M:%S", tm);
      //(void) strftime(timez, TIMESTRLEN, "%z", tm);
   }

   if (out != NULL)
   {
      len = 0;
      if (test_flag(LOGF_TIME))
      {
         const char *con, *coff;
         if (test_flag(LOGF_COLOR))
         {
            con = acol_[level_color(level)];
            coff = acol_[ANSI_RESET];
         }
         else
            con = coff = "";
#ifdef WITH_THREADS
      int id = sm_thread_id();
      pthread_mutex_lock(&mutex);
      len = fprintf(out, "%s.%03d %s (+%2d.%03d) %d:[%s%7s%s] ", timestr, (int) (tv.tv_usec / 1000), timez, (int) tr.tv_sec, (int) (tr.tv_usec / 1000), id, con, flty_[level], coff);
#else
      len = fprintf(out, "%s.%03d %s (+%2d.%03d) [%s%7s%s] ", timestr, (int) (tv.tv_usec / 1000), timez, (int) tr.tv_sec, (int) (tr.tv_usec / 1000), con, flty_[level], coff);
#endif
      }
      len += vfprintf(out, fmt, ap);
      len += fprintf(out, "\n");
#ifdef WITH_THREADS
      pthread_mutex_unlock(&mutex);
#endif
   }
   else
   {
      // log to syslog if no output stream is available
      //vsyslog(level | LOG_DAEMON, fmt, ap);
      len = vsnprintf(buf, SIZE_1K, fmt, ap);
      syslog(level | LOG_DAEMON, "%s", buf);

   }
   tv_stat = tv;
   return len;
}


/*! Log a message. This function automatically determines
 *  to which streams the message is logged.
 *  @param lf Log priority.
 *  @param fmt Format string.
 *  @param ... arguments
 */
int log_msg(int lf, const char *fmt, ...)
{
   va_list ap;
   int len;
#ifdef PRESERVE_ERRNO
   int err = errno;
#endif

   va_start(ap, fmt);
   len = vlog_msgf(log_, lf, fmt, ap);
   va_end(ap);

#ifdef PRESERVE_ERRNO
   errno = err;
#endif
   return len;
}


int log_errno(int lf, const char *s)
{
   return log_msg(lf, "%s: %s", s, strerror(errno));
}

