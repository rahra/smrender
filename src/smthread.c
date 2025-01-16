/* Copyright 2012-2025 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smthread.c
 * This file contains the code for multi-threaded execution of rules.
 *
 * \author Bernhard R. Fischer, <bf@abenteuerland.at>
 * \version 2024/01/14
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include "smrender.h"
#include "smcore.h"


#define SM_THREAD_EXEC 1
#define SM_THREAD_WAIT 0
#define SM_THREAD_EXIT -1


struct sm_thread
{
   const bx_node_t *tree;  // tree to traverse
   int depth;                 //!< depth to start traversal at
   int idx;                // leaf index
   th_param_t th_param;    //!< parameter to dhandler
   int result;             // result of dhandler
   int status;             // state of process (EXEC/WAIT/EXIT)
   pthread_t rule_thread;
};


void *sm_traverse_thread(struct sm_thread*);

//! mutex for thread structures
static pthread_mutex_t mmutex_ = PTHREAD_MUTEX_INITIALIZER;
//! condition of main thread
static pthread_cond_t mcond_ = PTHREAD_COND_INITIALIZER;
//! condition of rule threads
static pthread_cond_t tcond_ = PTHREAD_COND_INITIALIZER;
//! pointer to thread structures
static struct sm_thread *smth_ = NULL;
//! total number of threads
static int nthreads_ = 0;


/*! This function reads the number of CPUs from /proc/cpuinfo and returns it.
 * @return The function returns the number of processors found in
 * /proc/cpuinfo. If the file does not exist, -1 is returned. If the file
 * exists but no processors are found, 0 is returned.
 */
int get_ncpu(void)
{
   char buf[1024];
   FILE *cpuinfo;
   int n;

   if ((cpuinfo = fopen("/proc/cpuinfo", "r")) == NULL)
      return -1;

   for (n = 0; fgets(buf, sizeof(buf), cpuinfo) != NULL;)
      if (!strncmp(buf, "processor", 9))
         n++;

   fclose(cpuinfo);
   return n;
}


int init_threads(int nthreads)
{
   int i, e;

   if (nthreads < 1)
      nthreads = 1;
   nthreads_ = nthreads;

   log_msg(LOG_INFO, "initializing %d threads...", nthreads);
   if ((smth_ = calloc(nthreads, sizeof(*smth_))) == NULL)
   {
      log_errno(LOG_ERR, "calloc() failed:");
      return -1;
   }

   for (i = 0; i < nthreads; i++)
   {
      smth_[i].status = SM_THREAD_WAIT;
      smth_[i].th_param.id = i;
      smth_[i].th_param.cnt = nthreads;
      // FIXME: error handling should be improved!
      if ((e = pthread_create(&smth_[i].rule_thread, NULL, (void*(*)(void*)) sm_traverse_thread, &smth_[i])))
      {
         log_msg(LOG_ERR, "pthread_create() failed: %s", strerror(e));
         smth_[i].status = SM_THREAD_EXIT;
      }
   }

   return 0;
}


void __attribute__((destructor)) delete_threads(void)
{
   int i;

   sm_wait_threads();

   // instruct all threads to exit
   pthread_mutex_lock(&mmutex_);
   for (i = 0; i < nthreads_; i++)
      smth_[i].status = SM_THREAD_EXIT;
   pthread_cond_broadcast(&tcond_);
   pthread_mutex_unlock(&mmutex_);

   // join all threads
   for (i = 0; i < nthreads_; i++)
      pthread_join(smth_[i].rule_thread, NULL);

   free(smth_);
}


void *sm_traverse_thread(struct sm_thread *smth)
{
   sigset_t sset;
   int e;

   sigemptyset(&sset);
   if ((e = pthread_sigmask(SIG_BLOCK, &sset, NULL)))
      log_msg(LOG_ERR, "pthread_sigmask() failed: %s", strerror(e));

   for (;;)
   {
      pthread_mutex_lock(&mmutex_);
      while (smth->status != SM_THREAD_EXEC)
      {
         if (smth->status == SM_THREAD_EXIT)
         {
            pthread_mutex_unlock(&mmutex_);
            return NULL;
         }
         pthread_cond_wait(&tcond_, &mmutex_);
      }
      pthread_mutex_unlock(&mmutex_);

      // execute rule
      log_debug("thread %d executing action", smth->th_param.id);
      smth->result = traverse(smth->tree, smth->depth, smth->idx, smth->th_param.dhandler, &smth->th_param);

      pthread_mutex_lock(&mmutex_);
      smth->status = SM_THREAD_WAIT;
      pthread_cond_signal(&mcond_);
      pthread_mutex_unlock(&mmutex_);
   }

   return NULL;
}


void sm_wait_threads(void)
{
   int i;

   log_debug("waiting for all threads to finish action");
   for (i = 0; i < nthreads_; i++)
   {
      pthread_mutex_lock(&mmutex_);
      while (smth_[i].status == SM_THREAD_EXEC)
         pthread_cond_wait(&mcond_, &mmutex_);
      pthread_mutex_unlock(&mmutex_);
   }
}


int traverse_queue(const bx_node_t *tree, int d, int idx, tree_func_t dhandler, void *p)
{
   int i, res;

   // init thread parameters
   pthread_mutex_lock(&mmutex_);
   for (i = 0; i < nthreads_; i++)
   {
      // safety check
      if (smth_[i].status != SM_THREAD_WAIT)
         log_msg(LOG_ERR, "thread %d not ready!", smth_[i].th_param.id);

      smth_[i].tree = tree;
      smth_[i].depth = d;
      smth_[i].idx = idx;
      smth_[i].th_param.dhandler = dhandler;
      smth_[i].th_param.param = p;
      smth_[i].status = SM_THREAD_EXEC;
   }
   // signal threads to start
   pthread_cond_broadcast(&tcond_);
   pthread_mutex_unlock(&mmutex_);

   // wait for all threads to finish
   sm_wait_threads();

   // collect results
   res = 0;
   pthread_mutex_lock(&mmutex_);
   for (i = 0; i < nthreads_; i++)
   {
      // prefer most negative value
      if (smth_[i].result < 0 && smth_[i].result < res)
         res = smth_[i].result;
      // otherwise get most positive value
      else if (res >= 0 && smth_[i].result > res)
         res = smth_[i].result;
   }
   pthread_mutex_unlock(&mmutex_);

   return res;
}

