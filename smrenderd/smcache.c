/* Copyright 2014 Bernhard R. Fischer.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <time.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif

#include "smrender.h"
#include "smcache.h"


static struct qcache qc_[MAX_CACHE];
#ifdef WITH_THREADS
static pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;
#endif


qcache_t *qc_lookup(const struct bboxi *bb)
{
   qcache_t *qc = NULL;

   qc_lock(&mutex_);
   for (int i = 0; i < MAX_CACHE; i++)
   {
      if (qc_[i].age && !memcmp(&qc_[i].bb, bb, sizeof(*bb)))
      {
         log_debug("cache hit");
         qc_[i].age = time(NULL);
         qc_[i].ctr++;
         qc = &qc_[i];
         break;
      }
   }
   qc_unlock(&mutex_);
   return qc;
}


void qc_release(qcache_t *qc)
{
   qc_lock(&mutex_);
   qc->age = time(NULL);
   qc->ctr--;
   if (!qc->ctr)
   {
      qc_signal(&cond_);
   }
   qc_unlock(&mutex_);
}


static int qc_oldest(void)
{
   time_t t = time(NULL);
   int n = -1;

   for (int i = 0; i < MAX_CACHE; i++)
      if (!qc_[i].ctr && qc_[i].age < t)
      {
         t = qc_[i].age;
         n = i;
      }

   return n;
}


void qc_cleanup(void)
{
   bx_node_t *tree;
   int n;

   qc_lock(&mutex_);
   while ((n = qc_oldest()) == -1)
   {
      log_debug("all caches are in use, waiting...");
      qc_wait(&cond_, &mutex_);
   }
   tree = qc_[n].tree;
   qc_[n].age = 0;
   qc_unlock(&mutex_);

   bx_free_tree(tree);
}


qcache_t *qc_put(const struct bboxi *bb, bx_node_t *tree)
{
   qcache_t *qc = NULL;

   qc_lock(&mutex_);
   for (int i = 0; i < MAX_CACHE; i++)
   {
      if (!qc_[i].age)
      {
         qc_[i].bb = *bb;
         qc_[i].tree = tree;
         qc_[i].age = time(NULL);
         qc_[i].ctr = 1;
         qc = &qc_[i];
         break;
      }
   }
   qc_unlock(&mutex_);

   return qc;
}

