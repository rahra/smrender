/* Copyright 2008-2011 Bernhard R. Fischer, Daniel Haslinger.
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

/*! This file simply contains the logging functions. It was originally written
 * for OnionCat and was adapted to be used for smrender.
 * 
 *  @author Bernhard R. Fischer
 *  @version 2011/12/20
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
//! FILE pointer to log
static FILE *log_ = NULL;
static int level_ = LOG_INFO;


void __attribute__((constructor)) init_log0(void)
{
   log_ = stderr; 
   (void) sm_thread_id();
}


FILE *init_log(const char *s, int level)
{
   level_ = level;

   if (!strcmp(s, "stderr"))
      log_ = stderr;
   else if ((log_ = fopen(s, "a")) == NULL)
      fprintf(stderr, "*** could not open logfile %s: %s. Logging to syslog.\n", s, strerror(errno));

   return log_;
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
#ifdef WITH_THREADS
      int id = sm_thread_id();
      pthread_mutex_lock(&mutex);
      len = fprintf(out, "%s.%03d %s (+%2d.%03d) %d:[%7s] ", timestr, (int) (tv.tv_usec / 1000), timez, (int) tr.tv_sec, (int) (tr.tv_usec / 1000), id, flty_[level]);
#else
      len = fprintf(out, "%s.%03d %s (+%2d.%03d) [%7s] ", timestr, (int) (tv.tv_usec / 1000), timez, (int) tr.tv_sec, (int) (tr.tv_usec / 1000), flty_[level]);
#endif
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

