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
   int (*func)(smrule_t*, osm_obj_t*);
   smrule_t *rule;
   osm_obj_t *obj;
   int result;
   int status;
   pthread_mutex_t *mutex;
   pthread_cond_t *smr_cond;
   pthread_t rule_thread;
   pthread_cond_t rule_cond;
};


void *sm_rule_thread(struct sm_thread*);
void sm_wait_threads(void);


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
      pthread_cond_init(&smth[i].rule_cond, NULL);
      pthread_create(&smth[i].rule_thread, NULL, (void*(*)(void*)) sm_rule_thread, &smth[i]);
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


void *sm_rule_thread(struct sm_thread *smth)
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
      smth->result = smth->func(smth->rule, smth->obj);

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

   for (i = 0; i < SM_THREADS; i++)
   {
      pthread_mutex_lock(&mutex_);
      while (smth_[i].status == SM_THREAD_EXEC)
         pthread_cond_wait(&cond_, &mutex_);
      pthread_mutex_unlock(&mutex_);
   }
}


int sm_exec_rule(smrule_t *r, osm_obj_t *o, int(*func)(smrule_t*, osm_obj_t*))
{
   int i;

   if (!(r->act->flags & ACTION_THREADED))
      return func(r, o);

   pthread_mutex_lock(&mutex_);
   for (;;)
   {
      for (i = 0; i < SM_THREADS; i++)
      {
         if (smth_[i].status == SM_THREAD_WAIT)
         {
            if ((smth_[i].func == NULL) && smth_[i].status)
               log_msg(LOG_WARN, "last rule returned %d", smth_[i].status);
            smth_[i].func = func;
            smth_[i].rule = r;
            smth_[i].obj = o;
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


int sm_exec_rule(smrule_t *r, osm_obj_t *o, int(*func)(smrule_t*, osm_obj_t*))
{
   return func(r, o);
}


#endif

