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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "smrender.h"
#include "smem.h"


static int page_size_ = DEF_PAGESIZE;
static size_t alloc_size_ = 0, free_size_ = 0;
static sm_memlist_t *heapmem_;
static sm_mem_t *mem_;


void __attribute__((constructor)) mem_init(void)
{
   static sm_memlist_t heapmem[2];
   static sm_mem_t mem;

   heapmem[0].next = heapmem[0].prev = &heapmem[1];
   heapmem[1].next = heapmem[1].prev = &heapmem[0];
   if ((page_size_ = sysconf(_SC_PAGESIZE)) == -1)
   {
      page_size_ = DEF_PAGESIZE;
      log_msg(LOG_WARN, "sysconf failed: %s", strerror(errno));
   }
   else
      page_size_ -= sizeof(sm_memlist_t);
   heapmem_ = heapmem;
   mem_ = &mem;
   log_msg(LOG_WARN, "mem_init(), page_size_ = %d", page_size_);
}


void *mem_alloc(size_t size)
{
   sm_memlist_t *mem;

   size += sizeof(sm_memlist_t);
   if ((mem = malloc(size)) == NULL)
      log_msg(LOG_ERR, "malloc() failed in AllocMem(): %s", strerror(errno)),
         exit(EXIT_FAILURE);
    
   mem->size = size;
   mem->next = heapmem_;
   mem->prev = heapmem_->prev;
   heapmem_->prev->next = mem;
   heapmem_->prev = mem;
   alloc_size_ += size;
   return mem + 1;
}


void mem_free(void *p)
{
   sm_memlist_t *mem;

   if (p == NULL)
      return;

   mem = p - sizeof(sm_memlist_t);
   free_size_ += mem->size;
   mem->next->prev = mem->prev;
   mem->prev->next = mem->next;
   free(mem);
}


void del_pages(void *p)
{
   mem_free(p);
}


void *new_pages(int n)
{
   void *p;

   p = mem_alloc(n * page_size_);
   return p;
}


void del_ctrl_block(struct sm_memblock *mb)
{
   mem_free(mb);
}


struct sm_memblock *new_ctrl_block(void)
{
   struct sm_memblock *mb;

   mb = mem_alloc(sizeof(*mb));
   return mb;
}


void *block_alloc(struct sm_memblock *mb, int size)
{
   struct sm_memblock *new_alloc;

   // just to be on the safe side
   if (mb->size < size)
      log_msg(LOG_EMERG, "mb->size < size, this should never happen"),
         exit(EXIT_FAILURE);

   new_alloc = new_ctrl_block();
   new_alloc->size = size;
   new_alloc->addr = mb->addr;
   new_alloc->next = mem_->alloc_list;
   mem_->alloc_list = new_alloc;

   mb->addr += size;
   mb->size -= size;

   return new_alloc->addr;
}


int consolidate_free_list(void)
{
   struct sm_memblock **free_list, *next;
   int frag_cnt = 0;

   for (free_list = &mem_->free_list; *free_list != NULL; )
   {
      // count fragments
      frag_cnt += (*free_list)->size < page_size_;

      if ((*free_list)->next == NULL)
         break;

      if ((*free_list)->addr + (*free_list)->size == (*free_list)->next->addr &&
            (*free_list)->size + (*free_list)->next->size <= page_size_)
      {
         (*free_list)->size += (*free_list)->next->size;
         next = (*free_list)->next;
         (*free_list)->next = next->next;
         del_ctrl_block(next);
         frag_cnt--;
         continue;
      }
      free_list = &(*free_list)->next;
   }
   return frag_cnt;
}


int free_block_size(int size)
{
   size /= page_size_ + 1;
   return (size + 1) * page_size_;
}


void block_free(struct sm_memblock *mb)
{
   struct sm_memblock **fb;

   for (fb = &mem_->free_list; *fb != NULL; fb = &(*fb)->next)
   {
      // block to free is behind free block
      if ((*fb)->addr + (*fb)->size == mb->addr)
      {
         (*fb)->size += mb->size;
         del_ctrl_block(mb);
         return;
      }
      // block to free is before free block
      else if (mb->addr + mb->size == (*fb)->addr)
      {
         (*fb)->addr -= mb->size;
         (*fb)->size += mb->size;
         del_ctrl_block(mb);
         return;
      }
      else if (mb->addr < (*fb)->addr)
      {
         mb->next = *fb;
         *fb = mb;
         return;
      }
   }

   log_msg(LOG_ERR, "cannot block_free()");
}


void __attribute__((destructor)) sm_mem_free(void)
{
   struct sm_memblock *mb, *fb;

   for (mb = mem_->alloc_list; mb != NULL; mb = mb->next)
      block_free(mb);

   while (consolidate_free_list());

   for (mb = mem_->free_list; mb != NULL; )
   {
      del_pages(mb->addr);
      fb = mb;
      mb = mb->next;
      del_ctrl_block(fb);
   }
}


void *sm_alloc(int size)
{
   struct sm_memblock **free_list, *new_free;

   for (free_list = &mem_->free_list; *free_list != NULL; free_list = &(*free_list)->next)
      if ((*free_list)->size >= size)
         return block_alloc(*free_list, size);

   new_free = new_ctrl_block();
   new_free->size = SM_PAGES(size) * page_size_;
   new_free->addr = new_pages(SM_PAGES(size));
   new_free->next = mem_->free_list;
   mem_->free_list = new_free;

   return block_alloc(new_free, size);
}


void sm_free(void *p)
{
   struct sm_memblock **alloc_list, *mb;

   for (alloc_list = &mem_->alloc_list; *alloc_list != NULL; alloc_list = &(*alloc_list)->next)
   {
      if (p == (*alloc_list)->addr)
      {
         mb = *alloc_list;
         *alloc_list = mb->next;
         block_free(mb);
         (void) consolidate_free_list();
         return;
      }
   }
   log_msg(LOG_ERR, "cannot sm_free(%p), illegal address", p);
}


char *sm_strdup(const char *s)
{
   char *buf;

   buf = sm_alloc(strlen(s) + 1);
   strcpy(buf, s);
   return buf;
}


#ifdef TEST_SMEM

int main(int argc, char **argv)
{
   char *a, *b, *c;

   a = sm_strdup("Hello");
   b = sm_strdup(" ");
   c = sm_strdup("World!");
   printf("%s%s%s\n", a, b, c);
   sm_free(b);
   sm_free(a);
   sm_free(c);

   return 0;
}

#endif

