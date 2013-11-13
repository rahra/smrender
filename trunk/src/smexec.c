/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of Smrender.
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
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! 
 * 
 *
 *  @author Bernhard R. Fischer
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_EXECVPE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <inttypes.h>
//#include <features.h>

#include "smrender.h"
#include "smaction.h"
#include "smrender_dev.h"  // include for print_onode()

struct exec_ctrl
{
   FILE *fout;
   int cfd[3];
   char **arg, **env;
   pid_t pid;
   int osm_hdr;
   int last_cmd;
};

struct scode
{
   int code;
   char *desc;
};

enum {CMD_NEXT, CMD_CMT, CMD_GET, CMD_HELP};
enum {CHLD_OUT, CHLD_IN, CHLD_EIN};

#ifndef HAVE_EXECVPE
extern char **environ;
#endif


static void send_status(FILE *, int , const char *);


static int eclose(int fd)
{
   if (fd == -1)
      return 0;

   if (!close(fd))
      return 0;

   log_msg(LOG_ERR, "close(%d) failed: %s", fd, strerror(errno));
   return -1;
}


/*! This function reconnects filedescriptors after a fork. client_dst is
 * closed, client_end is dup'ed and paren_end is closed.
 * @return The function returns 0 on success and -1 on error.
 */
static int reconnect_fd(int parent_end, int client_end, int client_dst)
{
   if (eclose(client_dst)) return -1;
   if (dup(client_end) == -1)
   {
      log_msg(LOG_ERR, "dup(%d) failed: %s", client_end, strerror(errno));
      return -1;
   }
   if (eclose(client_end)) return -1;
   if (eclose(parent_end)) return -1;

   return 0;
}


static pid_t sub_shell(char * const arg[], char * const env[], int sub_pipe[3][2])
{
   pid_t pid;

   if (arg == NULL)
      return -1;

   for (int i = 0; arg[i] != NULL; i++)
      log_debug("arg[%d] = \"%s\"", i, arg[i]);
   for (int i = 0; env != NULL && env[i] != NULL; i++)
      log_debug("env[%d] = \"%s\"", i, env[i]);

   switch (pid = fork())
   {
      //error
      case -1:
         //perror("fork()");
         log_msg(LOG_ERR, "fork() failed: %s", strerror(errno));
         exit(1);

      // child
      case 0:
         log_debug("reconnecting fds");
         if (reconnect_fd(sub_pipe[0][1], sub_pipe[0][0], 0))
            exit(1);
         if (reconnect_fd(sub_pipe[1][0], sub_pipe[1][1], 1))
            exit(1);
         if (reconnect_fd(sub_pipe[2][0], sub_pipe[2][1], 2))
            exit(1);

#ifdef HAVE_EXECVPE
         if (execvpe(arg[0], arg, env) == -1)
#else
         environ = env;
         if (execvp(arg[0], arg) == -1)
#endif
            log_msg(LOG_ERR, "could not exec %s", arg[0]);
         exit(1);

      // parent
      default:
         log_debug("closing child ends if pipes");
         eclose(sub_pipe[0][0]);
         eclose(sub_pipe[1][1]);
         eclose(sub_pipe[2][1]);
   }
   return pid;
}

/*
static int shell_comm(int in, int out, int err_in)
{
   return 0;
}*/


static void close_pipes(int sub_pipe[3][2])
{
   for (int i = 0; i < 3; i++)
      for (int j = 0; j < 2; j++)
         eclose(sub_pipe[i][j]);
}


static void free_exec_ctrl(struct exec_ctrl *ec)
{
   free(ec->arg);
   free(ec->env);
   free(ec);
}

 
static int parse_exec_args(struct exec_ctrl *ec, char *cmd, fparam_t **fp)
{
   int argc, envc;
   char **list;
   int i;

   if (ec == NULL || fp == NULL)
   {
      errno = EFAULT;
      return -1;
   }

   if ((ec->arg = malloc(2 * sizeof(*ec->arg))) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
      return -1;
   }

   ec->arg[0] = cmd;
   ec->arg[1] = NULL;
   ec->env = NULL;
   envc = 0;
   argc = 1;

   for (; *fp != NULL; fp++)
   {
      if (!strcasecmp((*fp)->attr, "arg"))
      {
         if ((list = realloc(ec->arg, sizeof(*ec->arg) * (argc + 2))) == NULL)
         {
            log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
            return -1;
         }
         list[argc] = (*fp)->val;
         list[argc + 1] = NULL;
         ec->arg = list;
         argc++;
      }
      else if (!strcasecmp((*fp)->attr, "env"))
      {
         for (i = 0; i < envc; i++)
            // FIXME: Windows environment is case-insensitive
            // FIXME: This does not work because val contains something like "envvar=value"
            if (!strcmp(ec->env[i], (*fp)->val))
               break;
         if (i < envc)
         {
            log_msg(LOG_WARN, "duplicate environment found: %s", ec->env[i]);
            continue;
         }

         if ((list = realloc(ec->env, sizeof(*ec->env) * (envc + 2))) == NULL)
         {
            log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
            return -1;
         }
         list[envc] = (*fp)->val;
         list[envc + 1] = NULL;
         ec->env = list;
         envc++;
      }
 
   }

   return 0;
}

 
int act_exec_ini(smrule_t *r)
{
   struct exec_ctrl *ec;
   int sub_pipe[3][2];
   char *cmd;

   if ((cmd = get_param("cmd", NULL, r->act)) == NULL)
   {
      log_msg(LOG_ERR, "mandatory parameter 'cmd' missing");
      return 1;
   }

   memset(sub_pipe, -1, sizeof(sub_pipe));
   if (pipe(sub_pipe[0]) == -1)
   {
      log_msg(LOG_ERR, "pipe([0]) failed: %s", strerror(errno));
      return -1;
   }
   if (pipe(sub_pipe[1]) == -1)
   {
      log_msg(LOG_ERR, "pipe([0]) failed: %s", strerror(errno));
      close_pipes(sub_pipe);
      return -1;
   }
   if (pipe(sub_pipe[2]) == -1)
   {
      log_msg(LOG_ERR, "pipe([0]) failed: %s", strerror(errno));
      close_pipes(sub_pipe);
      return -1;
   }

   if ((ec = malloc(sizeof(*ec))) == NULL)
   {
      log_msg(LOG_ERR, "failed to malloc() exec_ctrl: %s", strerror(errno));
      close_pipes(sub_pipe);
      return -1;
   }

   ec->osm_hdr = get_param_bool("osmhdr", r->act);

   ec->cfd[CHLD_OUT] = sub_pipe[0][1];
   ec->cfd[CHLD_IN] = sub_pipe[1][0];
   ec->cfd[CHLD_EIN] = sub_pipe[2][0];

   if (parse_exec_args(ec, cmd, r->act->fp) == -1)
   {
      close_pipes(sub_pipe);
      free_exec_ctrl(ec);
   }

   log_msg(LOG_INFO, "creating subshell");
   ec->pid = sub_shell(ec->arg, ec->env, sub_pipe);
   r->data = ec;

   if ((ec->fout = fdopen(ec->cfd[CHLD_OUT], "w")) == NULL)
   {
      log_msg(LOG_ERR, "fdopen() failed: %s", strerror(errno));
      return -1;
   }

   r->data = ec;
   fprintf(ec->fout, "<?xml version='1.0' encoding='UTF-8'?>\n<smrender version='0.1' generator='%s'>\n",
         PACKAGE_STRING);

   log_msg(LOG_INFO, "forked process %d", (int) ec->pid);
   return 0;
}


static void close_free_exec_ctrl(struct exec_ctrl *ec)
{
   int status;

   fclose(ec->fout);
   eclose(ec->cfd[CHLD_IN]);
   eclose(ec->cfd[CHLD_EIN]);
   free_exec_ctrl(ec);
   waitpid(ec->pid, &status, 0);
   log_msg(LOG_INFO, "child exited with %d", WEXITSTATUS(status));
}


static void print_onode_osm(FILE *fout, osm_obj_t *o)
{
   fprintf(fout, "<osm version='0.6' generator='smrender'>\n");
   print_onode(fout, o);
   fprintf(fout, "</osm>\n");
}


static int strsh(char *s)
{
   int i;
   for (i = strlen(s); i > 0 && isspace(s[i - 1]); i--)
      s[i - 1] = '\0';
   return i;
}


static char *skipb(char *s)
{
   if (s == NULL)
      return s;
   for (; isspace(*s); s++);
   return s;
}


static void strtr(char *s)
{
   // safety check
   if (s == NULL)
      return;

   for (; *s != '\0'; s++)
      if (isspace(*s))
            *s = ' ';
}


static int parse_exec_cmd(char *buf, char **sptr)
{
   char *s;

   strtr(buf);
   buf = skipb(buf);

   if (!strsh(buf) || *buf == '#')
      return CMD_CMT;

   for (s = strtok_r(buf, " ", sptr); s != NULL; s = strtok_r(NULL, " ", sptr))
   {
      if (!strcmp(".", s))
         return CMD_NEXT;
      if (!strcmp("get", s))
         return CMD_GET;
      if (!strcmp("help", s))
         return CMD_HELP;
   }

   return -1;
}


static void exec_help(FILE *fout)
{
   fprintf(fout, "<!-- HELP\n"
         "get (node|way|relation) <id>     Retrieve OSM object.\n"
         ".                                Get next matching OSM object.\n"
         "-->\n"
         );
}


static int exec_next(FILE *fout, char **sptr, int *ni)
{
   char *s;
   long n;

   if ((s = strtok_r(NULL, " ", sptr)) == NULL)
   {
      *ni = 0;
      return 0;
   }

   errno = 0;
   n = strtol(s, NULL, 0);
   if (errno)
   {
      send_status(fout, 400, strerror(errno));
      return -1;
   }
   if (n < -128 || n > 127)
   {
      send_status(fout, 400, "-128 <= n <= 127");
      return -1;
   }
   *ni = n;
   return 0;
}

 
static int exec_get(char **sptr, osm_obj_t **o)
{
   int64_t id;
   int type;
   char *s;

   if ((s = strtok_r(NULL, " ", sptr)) == NULL)
      return -1;
   if (!strcmp("node", s))
      type = OSM_NODE;
   else if (!strcmp("way", s))
      type = OSM_WAY;
   else if (!strcmp("relation", s))
      type = OSM_REL;
   else
      return -2;
   if ((s = strtok_r(NULL, " ", sptr)) == NULL)
      return -3;
   errno = 0;
   id = strtoll(s, NULL, 0);
   if (errno)
      return -4;

   if ((*o = get_object(type, id)) == NULL)
      return 404;

   return 200;
}


static int fd_read(int fd, char *buf, int buflen)
{
   int len = -1;

   for (;;)
   {
      log_debug("reading from %d", fd);
      if ((len = read(fd, buf, buflen - 1)) >= 0)
      {
         buf[len] = '\0';
         break;
      }

      log_msg(LOG_ERR, "failed to read from stdout: %s", strerror(errno));
      if (errno != EINTR)
         break;
   }
   return len;
}

 
static void send_status(FILE *fout, int code, const char *xstr)
{
   const struct scode scode[] = {
      {500, "internal server error"},
      {404, "not found"},
      {200, "OK"},
      {400, "bad request"},
      {0, NULL}};
   int i;

   for (i = 0; scode[i].code; i++)
      if (code == scode[i].code)
      {
         fprintf(fout, "<status code=\"%d\">%s", scode[i].code, scode[i].desc);
         if (xstr != NULL)
            fprintf(fout, ", %s", xstr);
         fprintf(fout, "</status>\n");
         fflush(fout);
         return;
      }
   send_status(fout, 500, NULL);
}


static int exec_cli(struct exec_ctrl *ec)
{
   char buf[1024], *sptr;
   osm_obj_t *o;
   fd_set rset;
   int len, i, n, last, e, ret = 0;

   for (int next = 0; !next;)
   {
      FD_ZERO(&rset);
      for (i = 0, n = -1; i < 2; i++)
         if (ec->cfd[i + 1] != -1)
         {
            log_debug("adding fd %d", ec->cfd[i + 1]);
            FD_SET(ec->cfd[i + 1], &rset);
            if (n < ec->cfd[i + 1])
               n = ec->cfd[i + 1];
         }

      if (n == -1)
      {
         log_msg(LOG_WARN, "no open input streams of pid %d", (int) ec->pid);
         return 1;
      }

      log_debug("select()...");
      if ((n = select(n + 1, &rset, NULL, NULL, NULL)) == -1)
      {
         if (errno == EINTR)
         {
            log_debug("caught signal, restarting select");
         }
         else
         {
            log_msg(LOG_WARN, "select failed: %s", strerror(errno));
            next++;
         }
         continue;
      }
      log_debug("select() returned %d fds", n);

      if (!n)
      {
         log_msg(LOG_ERR, "no fds ready, breaking loop");
         next++;
         continue;
      }

      for (i = 0; n && i < 2; i++)
      {
         if (ec->cfd[i + 1] == -1)
            continue;

         if (FD_ISSET(ec->cfd[i + 1], &rset))
         {
            n--;
            if ((len = fd_read(ec->cfd[i + 1], buf, sizeof(buf))) == -1)
            {
               next++;
               continue;
            }

            log_debug("read(%d) returned %d", ec->cfd[i + 1], len);
            if (!len)
            {
               log_msg(LOG_NOTICE, "child closed writing end of [%d]=%d", i, ec->cfd[i + 1]);
               eclose(ec->cfd[i + 1]);
               ec->cfd[i + 1] = -1;
               continue;
            }

            switch (i)
            {
               // child wrote to stdout
               case 0:
                  last = ec->last_cmd;
                  ec->last_cmd = parse_exec_cmd(buf, &sptr);
                  switch (ec->last_cmd)
                  {
                     case CMD_CMT:
                        ec->last_cmd = last;
                        break;

                     case CMD_NEXT:
                        if (!exec_next(ec->fout, &sptr, &ret))
                           next++;
                        break;

                     case CMD_HELP:
                        exec_help(ec->fout);
                        send_status(ec->fout, 200, NULL);
                        break;

                     case CMD_GET:
                        if ((e = exec_get(&sptr, &o)) <= 0)
                        {
                           log_msg(LOG_INFO, "exec_get() returned %d", e);
                           send_status(ec->fout, 400, NULL);
                        }
                        else
                        {
                           if (e == 200 && o != NULL)
                           {
                              if (!ec->osm_hdr)
                                 print_onode(ec->fout, o);
                              else
                                 print_onode_osm(ec->fout, o);
                           }
                           send_status(ec->fout, e, NULL);
                        }
                        break;

                     default:
                        log_msg(LOG_ERR, "unknown command '%s'", buf);
                        send_status(ec->fout, 400, NULL);
                  }
                  break;

               // child wrote to stderr
               case 1:
                  log_msg(LOG_ERR, "stderr[%d]: %.*s", (int) ec->pid, len, buf);
                  break;
            }
         }
      }
   }
   return ret;
}


int act_exec_main(smrule_t *r, osm_obj_t *o)
{
   struct exec_ctrl *ec = r->data;

   if (ec == NULL)
      return 0;

   if (!ec->osm_hdr)
      print_onode(ec->fout, o);
   else
      print_onode_osm(ec->fout, o);
   send_status(ec->fout, 200, NULL);

   return exec_cli(ec);
}


int act_exec_fini(smrule_t *r)
{
   struct exec_ctrl *ec = r->data;

   if (ec == NULL)
      return 0;

   send_status(ec->fout, 404, NULL);
   (void) exec_cli(ec);
   fprintf(ec->fout, "</smrender>\n");
   fflush(ec->fout);
   close_free_exec_ctrl(ec);

   return 0;
}

