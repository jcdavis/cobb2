#include "dline.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

/* Functions that operate on a data line (henceforth shortened dline).
 * The dline is the fundamental storage mechanism for data, it is used as
 * elements of hash tables as well as terminating suffixes at trie nodes.
 * dline is a single contiguous block of memory that is reallocated on every
 * update. The layout is as follows:
 * 1. 16 byte dline_entry metadata header
 * 2. Any number of non-null-terminated data characters, whose length is
 * stored as the len field in the preceding dline_etry.
 * 3. 0 to 7 bytes of buffer padding, so that the dline_entry for the next
 * suffix is allocated on a 8 byte boundary.
 * 
 * The termination of a dline is represented as the start of a dline_entry
 * with a magic global_ptr
 *
 * Suffixes are stored in score sorted order. In actual usage, dlines are
 * immutable: all operations create the a copy of the a dline with the given
 * update applied to them.
 */
 
/* Since a real global_ptr is malloc'ed, it is going to be aligned. This is
 * not, so no worries about collision.
 */
#define DLINE_MAGIC_TERMINATOR (char*)0x0011223344556677UL

/* size of the unused buffer between the end of a string and the start
 * of its next dline_entry,
 */
static inline size_t wasted_buffer(size_t str_len) {
  return (8-(str_len&7))&7;  
}

/* Size of a dline entry with a string of the given length, including bytes
 * beyond the end of the string for padding.
 */
static inline size_t entry_size(size_t str_len) {
  return sizeof(dline_entry) + str_len + wasted_buffer(str_len);
}

/* Given the address of a current entry, get the address of the next. */
static inline dline_entry* next_entry(dline_entry* current) {
  char* ptr = (char*)current;
  return (dline_entry*)(ptr + entry_size(current->len));
}

/* Pointer to where string starts for a given dline_entry. */
static inline char* str_offset(dline_entry* entry) {
  return ((char*)entry) + sizeof(dline_entry);
}

/* Returns a copy of the given dline with the new suffix inserted, or
 * resorted if the suffix is already present
 * If the current dline is null, this just creates a new dline with the
 * new element in it. Returns NULL on failure
 */
dline* dline_upsert(dline* existing, /* dline to perform upset on*/
                   char* string, /* full null-terminated string to upsert */
                   unsigned int start, /*byte offset from start of string*/
                   unsigned int total_len, /*NOT including null terminator*/
                   char** global_ptr, /* will be created if first insert*/
                   int score, /* score to set for upsert */
                   short* dline_upsert_mode, /* also set on first call*/
                   int* old_score) { /*set if the first call is an update */
  if(string == NULL)
    return NULL;
  
  dline* new_dline = NULL;
  unsigned int new_str_len = total_len-start;
  
  
  if(existing == NULL) {
    /*if the dline is NULL, we can't possibily be doing an update*/
    assert(*dline_upsert_mode != DLINE_UPSERT_MODE_UPDATE);
    
    /* Create the new dline for this suffix plus space for the trailing
     * magic terminator pointer.
     */
    new_dline = (dline*)malloc(entry_size(new_str_len) + sizeof(void*));
    
    if(new_dline == NULL) {
      return NULL;
    }
    
    if(*global_ptr == NULL) {
      assert(*dline_upsert_mode == DLINE_UPSERT_MODE_INITIAL);
      /*first suffix insert, so create the global_ptr*/
      *global_ptr = malloc(total_len+1);
      
      if(*global_ptr == NULL) {
        free(new_dline);
        return NULL;
      }
      
      strncpy(*global_ptr, string, total_len + 1);
    }
    
    dline_entry* new_entry = (dline_entry*)new_dline;
    
    /*set up our new dline*/
    new_entry->global_ptr = *global_ptr;
    new_entry->score = score;
    new_entry->len = new_str_len;
    memcpy(str_offset(new_entry), string+start, new_str_len);
    /*trailing magic pointer to indicate end of a dline.*/
    next_entry(new_entry)->global_ptr = DLINE_MAGIC_TERMINATOR;
    
    *dline_upsert_mode = DLINE_UPSERT_MODE_INSERT;
  } else if(*dline_upsert_mode == DLINE_UPSERT_MODE_INITIAL) {
    /* If we are upserting the first suffix, we don't yet know if this
     * will be an update or insert. Therefore we may need to scan the entire 
     * dline to see if a global string already exists, and if so what 
     * it's existing score is. Further suffix upserts will know which 
     * operation to perform, which saves us some comparisons.
     */
    assert(*global_ptr == NULL);
    
    dline_entry* current = existing;
    
    /* Not sure whether or not comparing the local suffix first ends up
     * being faster, but it should be. Those characters are almost always
     * in cache, and the global string is almost always not. But really, it
     * is unecessary, since finding an identical global_str is enough.
     */
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
          (strncmp(str_offset(current), string + start, new_str_len) ||
           strncmp(current->global_ptr, string, total_len))) {
      current = next_entry(current);        
    }
    
    if(current->global_ptr == DLINE_MAGIC_TERMINATOR) {
      /* We didn't find an entry, do an insert. Re-looping isn't the most
       * efficient way of doing things, but it'll work for now
       */
      *dline_upsert_mode = DLINE_UPSERT_MODE_INSERT;
      new_dline = dline_upsert(existing, string, start, total_len, global_ptr, score, dline_upsert_mode, old_score);
    } else {
      /* ran into a identical suffix with a full string identical to ours,
       * so this is an update. Similar to above, this just recurses for now.
       */
      *dline_upsert_mode = DLINE_UPSERT_MODE_UPDATE;
      *global_ptr = current->global_ptr;
      *old_score = current->score;
      new_dline = dline_upsert(existing, string, start, total_len,
                               global_ptr, score, dline_upsert_mode,
                               old_score);
    }
    
  } else if(*dline_upsert_mode == DLINE_UPSERT_MODE_INSERT) {
    /* Doing an insert, loop until the first item whose score is <= the new
     * suffix's score, and then copy the data before, insert the new suffix,
     * and copy the data after.
     */
    
    dline_entry* current = existing;
    uint64_t before_offset, after_offset = 0;
    
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
          current->score > score) {
      current = next_entry(current);
    }
    
    before_offset = (uint64_t)current - (uint64_t)existing;
    
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR) {
      current = next_entry(current);
    }
    
    after_offset = (uint64_t)current - (uint64_t)existing - before_offset;
    
    new_dline = (dline*)malloc(before_offset + entry_size(new_str_len) +
                               after_offset + sizeof(void*));
    
    if(new_dline == NULL) {
      return NULL;
    }
    
    if(*global_ptr == NULL) {
      /*first suffix insert, so create the global_ptr*/
      *global_ptr = malloc(total_len+1);
      
      if(*global_ptr == NULL) {
        free(new_dline);
        return NULL;
      }
      
      strncpy(*global_ptr, string, total_len + 1);
    }
    
    /*copy over entries before our new entry*/
    memcpy(new_dline, existing, before_offset);
    
    dline_entry* new_entry = (dline_entry*)((char*)new_dline +
                                            before_offset);
    
    /*set up our new dline*/
    new_entry->global_ptr = *global_ptr;
    new_entry->score = score;
    new_entry->len = new_str_len;
    memcpy(str_offset(new_entry), string + start, new_str_len);
    
    /*copy over entries after our new entry*/
    memcpy(((char*)new_dline) + before_offset + entry_size(new_str_len),
           ((char*)existing) + before_offset, after_offset);
    /*termiante with magic (Ugh Ugly)*/
    ((dline_entry*)((char*)new_dline + before_offset +
                   entry_size(new_str_len) + after_offset))->global_ptr =
                   DLINE_MAGIC_TERMINATOR;
    
  } else if(*dline_upsert_mode == DLINE_UPSERT_MODE_UPDATE) {
    /* If surely doing an update, the previous score has been identified.
     * First seek to the higher of score and old_score, and then the lower.
     * Using the found offsets, copy over old dline without the old entry
     * and with the new entry add.
     */
    assert(*global_ptr != NULL);
    
  } /*else invalid mode, just return NULL*/
  
  return new_dline;
}

/* Search the given dline for suffixes starting with string[start] and
 * minimum score of min_score. Stores result_size number of entries in
 * results, and returns the number of results stored there.
 */
int dline_search(dline* dline,
                 char* string,
                 unsigned int start,
                 unsigned int total_len,
                 int min_score,
                 dline_entry* results,
                 int result_size) {
  dline_entry* current = (dline_entry*)dline;
  int num_found = 0;
  unsigned int match_len = total_len-start;
  
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
        current->score >= min_score) {
    if(match_len <= current->len &&
       !strncmp(string+start, str_offset(current), match_len)) {
      results[num_found].global_ptr = current->global_ptr;
      results[num_found].score = current->score;
      results[num_found].len = current->len;
      num_found++;
      if(num_found == result_size)
        break;
    }
    
    current = next_entry(current);
  }
  
  return num_found;
}

/* Simple debugging function which outputs all of the contents of a dline.
 */
void dline_debug(dline* dline) {
  dline_entry* current = (dline_entry*)dline;
  printf("dline at 0x%llx\n", (uint64_t)current); 
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR) {
    /* bs required to add a null terminator, else we would have to scanf
     * the printf string to add precision since the length isn't known in
     * advance (even uglier).
     */
    char* tmp = (char*)malloc(current->len+1);
    assert(tmp != NULL);
    strncpy(tmp, (char*)current + sizeof(dline_entry), current->len);
    tmp[current->len] = '\0';
    printf("ptr: %p\nlen: %u\nscr: %u\n[%s]\n", current->global_ptr,
                                                 current->len,
                                                 current->score,
                                                 tmp);
    free(tmp);
    current = next_entry(current);
  }
  printf("Total length: %llu\n", (uint64_t)current - (uint64_t)dline +
         sizeof(void*));
}