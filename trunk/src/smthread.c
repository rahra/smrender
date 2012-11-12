/* Copyright 2012 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

/*! This file contains the code for multi-threaded execution of rules.
 *
 *  @author Bernhard R. Fischer
 */

#include "smrender_dev.h"

#include <string.h>
#include <pthread.h>


#ifdef WITH_THREADS


#ifndef SM_THREADS
#define SM_THREADS 4
#endif

#define SM_THREAD_EXEC 1
#define SM_THREAD_WAIT 0
#define SM_THREAD_EXIT -1


struct sm_thread
{
   const bx_node_t *tree;  // tree to traverse
   int idx;                // leaf index
   tree_func_t dhandler;   // function to call at each tree node
   void *param;            // parameter to dhandler
   int result;             // result of dhandler
   int status;             // state of process (EXEC/WAIT/EXIT)
   int nr;                 // number of thread
   pthread_mutex_t *mutex;
   pthread_cond_t *smr_cond;
   pthread_t rule_thread;
   pthread_cond_t rule_cond;
};


void *sm_traverse_thread(struct sm_thread*);


static pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;
static struct sm_thread *smth_;
 

void __attribute__((constructor)) init_threads(void)
{
   static struct sm_thread smth[SM_THREADS];
   int i;

   memset(smth, 0, sizeof(smth));
   smth_ = smth;
   for (i = 0; i < SM_THREADS; i++)
   {
      smth[i].status = SM_THREAD_WAIT;
      smth[i].mutex = &mutex_;
      smth[i].smr_cond = &cond_;
      smth[i].nr = i;
      pthread_cond_init(&smth[i].rule_cond, NULL);
      pthread_create(&smth[i].rule_thread, NULL, (void*(*)(void*)) sm_traverse_thread, &smth[i]);
   }
}


void __attribute__((destructor)) delete_threads(void)
{
   int i;

   sm_wait_threads();
   for (i = 0; i < SM_THREADS; i++)
   {
      pthread_mutex_lock(&mutex_);
      smth_[i].status = SM_THREAD_EXIT;
      pthread_cond_signal(&smth_[i].rule_cond);
      pthread_mutex_unlock(&mutex_);
      pthread_join(smth_[i].rule_thread, NULL);
      pthread_cond_destroy(&smth_[i].rule_cond);
   }
   pthread_cond_destroy(&cond_);
   pthread_mutex_destroy(&mutex_);
}


int sm_thread_id(void)
{
   int i;

   for (i = 0; i < SM_THREADS; i++)
      if (pthread_self() == smth_[i].rule_thread)
         return i + 1;
   return 0;
}


void *sm_traverse_thread(struct sm_thread *smth)
{
   for (;;)
   {
      pthread_mutex_lock(smth->mutex);
      while (smth->status != SM_THREAD_EXEC)
      {
         if (smth->status == SM_THREAD_EXIT)
         {
            pthread_mutex_unlock(smth->mutex);
            return NULL;
         }
         //if (smth->status == SM_THREAD_WAIT)
         pthread_cond_wait(&smth->rule_cond, smth->mutex);
      }
      pthread_mutex_unlock(smth->mutex);

      // execute rule
      log_debug("thread %d executing action %p", smth->nr, smth->dhandler);
      //smth->result = smth->func(smth->rule, smth->obj);
      smth->result = traverse(smth->tree, 0, smth->idx, smth->dhandler, NULL, smth->param);

      pthread_mutex_lock(smth->mutex);
      smth->status = SM_THREAD_WAIT;
      pthread_cond_signal(smth->smr_cond);
      pthread_mutex_unlock(smth->mutex);
   }

   return NULL;
}


void sm_wait_threads(void)
{
   int i;

   log_debug("waiting for all threads to finish action");
   for (i = 0; i < SM_THREADS; i++)
   {
      pthread_mutex_lock(&mutex_);
      while (smth_[i].status == SM_THREAD_EXEC)
         pthread_cond_wait(&cond_, &mutex_);
      pthread_mutex_unlock(&mutex_);
   }
}


int traverse_queue(const bx_node_t *tree, int idx, tree_func_t dhandler, void *p)
{
   int i;

   //log_debug("looking up thread");
   pthread_mutex_lock(&mutex_);
   for (;;)
   {
      for (i = 0; i < SM_THREADS; i++)
      {
         if (smth_[i].status == SM_THREAD_WAIT)
         {
            if (smth_[i].result)
               log_msg(LOG_WARN, "last traverse returned %d", smth_[i].result);
            smth_[i].tree = tree;
            smth_[i].idx = idx;
            smth_[i].dhandler = dhandler;
            smth_[i].param = p;
            smth_[i].status = SM_THREAD_EXEC;
            pthread_cond_signal(&smth_[i].rule_cond);
            break;
         }
      }

      if (i < SM_THREADS)
         break;

      pthread_cond_wait(&cond_, &mutex_);
   }
   pthread_mutex_unlock(&mutex_);

   return 0;
}


#else


int sm_thread_id(void)
{
   return 0;
}


#endif


int sm_is_threaded(const smrule_t *r)
{
   return (r->act->flags & ACTION_THREADED) != 0;
}


void sm_threaded(smrule_t *r)
{
   log_debug("activating multi-threading for rule 0x%016lx", r->oo->id);
   r->act->flags |= ACTION_THREADED;
}

