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
 * \version 2024/01/23
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


void *sm_thread_loop(sm_thread_t*);

//! mutex for thread structures
static pthread_mutex_t mmutex_ = PTHREAD_MUTEX_INITIALIZER;
//! condition of main thread
static pthread_cond_t mcond_ = PTHREAD_COND_INITIALIZER;
//! pointer to thread structures
static sm_thread_t *smth_ = NULL;
//! total number of threads
static int nthreads_ = 0;
//! max number of objs in obj list
static int obj_max_ = 1024;
//! current thread id to queue objects to
static int cur_id_ = -1;


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


/*! This function initializes the threads for rule parallel processing.
 * @param nthreads Number of threads to initialize, 0 <= nthreads.
 * @return This function returns the number if threads initialized.
 */
int init_threads(int nthreads)
{
   static sm_thread_t _smth; // static fallback memory
   int i, e;

   if (nthreads < 0)
      nthreads = 0;

   log_msg(LOG_INFO, "initializing %d threads...", nthreads);
   if ((smth_ = calloc(nthreads + 1, sizeof(*smth_))) == NULL)
   {
      log_errno(LOG_ERR, "calloc() failed:");

      log_msg(LOG_NOTICE, "continuing without threads");
      nthreads = 0;
      memset(&_smth, 0, sizeof(_smth));
      smth_ = &_smth;
   }

   if ((smth_[0].obj = malloc(sizeof(*smth_[0].obj) * obj_max_ * (nthreads + 1))) == NULL)
   {
      free(smth_);
      log_errno(LOG_ERR, "malloc() failed");
      log_msg(LOG_NOTICE, "continuing without threads");
      nthreads = 0;
      memset(&_smth, 0, sizeof(_smth));
      smth_ = &_smth;
   }

   nthreads_ = nthreads;
   for (i = 0; i < nthreads; i++)
   {
      smth_[i].status = SM_THREAD_WAIT;
      smth_[i].id = i;
      smth_[i].cnt = nthreads;
      smth_[i].obj = smth_[0].obj + obj_max_ * i;
      pthread_cond_init(&smth_[i].cond, NULL);
      // FIXME: error handling should be improved!
      if ((e = pthread_create(&smth_[i].thandle, NULL, (void*(*)(void*)) sm_thread_loop, &smth_[i])))
      {
         log_msg(LOG_ERR, "pthread_create() failed: %s", strerror(e));
         smth_[i].status = SM_THREAD_EXIT;
      }
   }

   // set values for main thread
   smth_[nthreads].thandle = pthread_self();
   smth_[nthreads].id = nthreads;
   smth_[nthreads].cnt = nthreads;
   smth_[nthreads].obj = smth_[0].obj + obj_max_ * nthreads;

   return nthreads;
}


/*! Return the number of threads.
 * @return Returns the number of returns which is always >= 0.
 */
int get_nthreads(void)
{
   return nthreads_;
}


/*! Return internal thread id of calling thread.
 * @return Returns id of calling thread.
 */
int get_thread_id(void)
{
   int i = 0;
   for (pthread_t id = pthread_self(); i < nthreads_ && !pthread_equal(id, smth_[i].thandle); i++);
   return smth_[i].id;
}


sm_thread_t *get_th_param(int n)
{
   return &smth_[n];
}


void __attribute__((destructor)) delete_threads(void)
{
   int i;

   sm_wait_threads();

   // instruct all threads to exit
   pthread_mutex_lock(&mmutex_);
   for (i = 0; i < nthreads_; i++)
   {
      smth_[i].status = SM_THREAD_EXIT;
      pthread_cond_signal(&smth_[i].cond);
   }
   pthread_mutex_unlock(&mmutex_);

   // join all threads
   for (i = 0; i < nthreads_; i++)
   {
      pthread_join(smth_[i].thandle, NULL);
      pthread_cond_destroy(&smth_[i].cond);
   }

   free(smth_);
}


void *sm_thread_loop(sm_thread_t *smth)
{
   sigset_t sset;
   int e, res;

   sigemptyset(&sset);
   if ((e = pthread_sigmask(SIG_BLOCK, &sset, NULL)))
      log_msg(LOG_ERR, "pthread_sigmask() failed: %s", strerror(e));

   for (;;)
   {
      log_debug("thread %d waiting for objects", smth->id);
      pthread_mutex_lock(&mmutex_);
      while (smth->status != SM_THREAD_EXEC)
      {
         if (smth->status == SM_THREAD_EXIT)
         {
            pthread_mutex_unlock(&mmutex_);
            return NULL;
         }
         pthread_cond_wait(&smth->cond, &mmutex_);
      }
      pthread_mutex_unlock(&mmutex_);

      // execute rule
#if defined(TH_OBJ_LIST)
      log_debug("processing object list");
      for (res = 0; !res && smth->obj_cnt;)
      {
         smth->obj_cnt--;
         res = smth->main(smth->param, smth->obj[smth->obj_cnt]);
      }
#endif

      pthread_mutex_lock(&mmutex_);
      smth->result = res;
      smth->status = SM_THREAD_WAIT;
      pthread_cond_signal(&mcond_);
      pthread_mutex_unlock(&mmutex_);
   }

   return NULL;
}


/*! This function blocks as long as at least one thread is execution, i.e. its
 * state == SM_THREAD_EXEC. The mutex mmutex_ must be acquired before calling
 * this function.
 */
void block_while_exec(void)
{
   for (int i = 0;;)
   {
      for (; i < nthreads_ && smth_[i].status != SM_THREAD_EXEC; i++);

      if (i >= nthreads_)
         return;

      pthread_cond_wait(&mcond_, &mmutex_);
   }
}


/*! Wait for all threads to finish execution, i.e. their state becomes !=
 * SM_THREAD_EXEC.
 */
void sm_wait_threads(void)
{
   log_debug("waiting for all threads to finish action");
   pthread_mutex_lock(&mmutex_);
   block_while_exec();
   pthread_mutex_unlock(&mmutex_);
   log_debug("threads ready");
}


/*! This function returns the number if a free (not working) thread, i.e. whose
 * status is SM_THREAD_WAIT. If no thread is ready it waits for the condition
 * to be signalled.
 * @return Returns the number of the thread which is 0 <= n < nthreads_.
 */
int get_free_thread(void)
{
   for (;;)
   {
      for (int i = 0; i < nthreads_; i++)
         if (smth_[i].status == SM_THREAD_WAIT)
            return i;
      pthread_cond_wait(&mcond_, &mmutex_);
   }
}


void obj_queue_ini(int (*main)(void*, osm_obj_t*), void *p)
{
   cur_id_ = -1;
   pthread_mutex_lock(&mmutex_);
   for (int n = 0; n < nthreads_; n++)
   {
      smth_[n].main = main;
      smth_[n].param = p;
      //smth_[n].obj_cnt = 0;
   }
   pthread_mutex_unlock(&mmutex_);
}


int obj_queue(osm_obj_t *obj)
{
   int res;

   if (cur_id_ < 0)
   {
      pthread_mutex_lock(&mmutex_);
      cur_id_ = get_free_thread();

      // save previous result
      if ((res = smth_[cur_id_].result))
      {
         smth_[cur_id_].obj_cnt = 0;
         smth_[cur_id_].result = 0;
         smth_[cur_id_].status = SM_THREAD_WAIT;
         pthread_mutex_unlock(&mmutex_);
         return res;
      }
      pthread_mutex_unlock(&mmutex_);
   }

   // add obj to obj list
   smth_[cur_id_].obj[smth_[cur_id_].obj_cnt++] = obj;

   if (smth_[cur_id_].obj_cnt >= obj_max_)
   {
      log_debug("signalling thread %d to process objects", cur_id_);
      // signal threads to start
      pthread_mutex_lock(&mmutex_);
      smth_[cur_id_].status = SM_THREAD_EXEC;
      pthread_cond_signal(&smth_[cur_id_].cond);
      pthread_mutex_unlock(&mmutex_);

      cur_id_ = -1;
   }

   return 0;
}


void obj_queue_signal(void)
{
   //FIXME: check result
   log_debug("signalling threads for remaining objects");
   pthread_mutex_lock(&mmutex_);
   for (int n = 0; n < nthreads_; n++)
      if (smth_[n].obj_cnt && smth_[n].status == SM_THREAD_WAIT)
      {
         smth_[n].status = SM_THREAD_EXEC;
         pthread_cond_signal(&smth_[n].cond);
      }
   pthread_mutex_unlock(&mmutex_);
}

