#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "dline.h"
#include "cobb2.h"

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
#define DLINE_MAGIC_TERMINATOR (global_data*)0x0011223344556677UL

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

static inline global_data* create_global(char* string, int total_len) {
  global_data* result = malloc(sizeof(global_data) + total_len + 1);
  if(result == NULL)
    return NULL;
  result->len = total_len;
  memcpy(GLOBAL_STR(result), string, total_len + 1);
  
  return result;
}

/* Creates a copy of the given dline with the insert/update applied. If the
 * existing line is NULL, creates a new one with just the single element.
 * Returns status code of the operation.
 */
op_result dline_upsert(dline_t* existing, /* dline to perform upset on*/
                      dline_t** result, /* resulting dline*/
                      char* string,/*full null-terminated string to upsert*/
                      unsigned int start,/*offset from start of string*/
                      unsigned int total_len,/*NOT including \0 terminator*/
                      int score, /*score to set*/
                      upsert_state* state) {/*set on first call*/
  if(string == NULL || result == NULL || state == NULL)
    return BAD_PARAM;
  
  unsigned int suffix_len = total_len-start;
  
  
  if(existing == NULL) {
    /*if the dline is NULL, we can't possibily be doing an update*/
    assert(state->mode != UPSERT_MODE_UPDATE);
    
    /* Create the new dline for this suffix plus space for the trailing
     * magic terminator pointer.
     */
    *result = (dline_t*)malloc(entry_size(suffix_len) + sizeof(void*));
    
    if(*result == NULL) {
      return MALLOC_FAIL;
    }
    
    if(state->global_ptr == NULL) {
      assert(state->mode == UPSERT_MODE_INITIAL);
      /*first suffix insert, so create the global_ptr*/
      state->global_ptr = create_global(string, total_len);
      
      if(state->global_ptr == NULL) {
        free(*result);
        return MALLOC_FAIL;
      }
    }
    
    dline_entry* new_entry = (dline_entry*)*result;
    
    /*set up our new dline_t*/
    new_entry->global_ptr = state->global_ptr;
    new_entry->score = score;
    new_entry->len = suffix_len;
    memcpy(str_offset(new_entry), string+start, suffix_len);
    /*trailing magic pointer to indicate end of a dline.*/
    next_entry(new_entry)->global_ptr = DLINE_MAGIC_TERMINATOR;
    
    state->mode = UPSERT_MODE_INSERT;
    
    return NO_ERROR;
    
  } else if(state->mode == UPSERT_MODE_INITIAL) {
    /* If we are upserting the first suffix, we don't yet know if this
     * will be an update or insert. Therefore we may need to scan the entire 
     * dline to see if a global string already exists, and if so what 
     * it's existing score is. Further suffix upserts will know which 
     * operation to perform, which saves us some comparisons.
     */
    assert(state->global_ptr == NULL);
    
    dline_entry* current = existing;
    
    /* Not sure whether or not comparing the local suffix first ends up
     * being faster, but it should be. Those characters are almost always
     * in cache, and the global string is almost always not. But really, it
     * is unecessary, since finding an identical global_str is enough.
     */
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
          (suffix_len != current->len ||
           memcmp(str_offset(current), string + start, suffix_len) ||
           current->global_ptr->len != total_len ||
           memcmp(GLOBAL_STR(current->global_ptr), string, total_len))) {
      current = next_entry(current);        
    }
    
    if(current->global_ptr == DLINE_MAGIC_TERMINATOR) {
      /* We didn't find an entry, do an insert. Re-looping isn't the most
       * efficient way of doing things, but it'll work for now
       */
      state->mode = UPSERT_MODE_INSERT;
      return dline_upsert(existing, result, string, start, total_len,
                          score, state);
    } else {
      /* ran into a identical suffix with a full string identical to ours,
       * so this is an update. Similar to above, this just recurses for now.
       */
      state->mode = UPSERT_MODE_UPDATE;
      state->global_ptr = current->global_ptr;
      state->old_score = current->score;
      return dline_upsert(existing, result, string, start, total_len, 
                          score, state);
    }
    
    return NO_ERROR;
    
  } else if(state->mode == UPSERT_MODE_INSERT) {
    /* Doing an insert, loop until the first item whose score is <= the new
     * suffix's score, and then copy the data before, insert the new suffix,
     * and copy the data after.
     */
    
    dline_entry* current = existing;
    uint64_t before_size, after_size = 0;
    
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
          current->score > score) {
      current = next_entry(current);
    }
    
    before_size = (uint64_t)current - (uint64_t)existing;
    
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR) {
      current = next_entry(current);
    }
    
    after_size = (uint64_t)current - (uint64_t)existing - before_size;
    
    *result = (dline_t*)malloc(before_size + entry_size(suffix_len) +
                               after_size + sizeof(void*));
    
    if(*result == NULL) {
      return MALLOC_FAIL;
    }
    
    if(state->global_ptr == NULL) {
      /*first suffix insert, so create the global_ptr*/
      state->global_ptr = create_global(string, total_len);
      
      if(state->global_ptr == NULL) {
        free(*result);
        return MALLOC_FAIL;
      }
    }
    
    /*copy over entries before our new entry*/
    memcpy(*result, existing, before_size);
    
    dline_entry* new_entry = (dline_entry*)((char*)*result +
                                            before_size);
    
    /*set up our new dline_t*/
    new_entry->global_ptr = state->global_ptr;
    new_entry->score = score;
    new_entry->len = suffix_len;
    memcpy(str_offset(new_entry), string + start, suffix_len);
    
    /*copy over entries after our new entry*/
    memcpy(((char*)*result) + before_size + entry_size(suffix_len),
           ((char*)existing) + before_size, after_size);
    /*termiante with magic (Ugh Ugly)*/
    ((dline_entry*)((char*)*result + before_size +
                   entry_size(suffix_len) + after_size))->global_ptr =
                   DLINE_MAGIC_TERMINATOR;
    
    return NO_ERROR;
  } else if(state->mode == UPSERT_MODE_UPDATE) {
    /* If surely doing an update, the previous score has been identified.
     * First seek to the higher of score and old_score, and then the lower.
     * Using the found offsets, copy over old dline without the old entry
     * and with the new entry add.
     */
    assert(state->global_ptr != NULL);
    
    /*TODO: DO THIS, IT SUCKS*/
    return NO_ERROR;
  } else {
    return BAD_PARAM;
  }
}

/* Removes a suffix from a dline by creating a copy without the removed
 * elements, and returns a result code
 */
op_result dline_remove(dline_t* existing,
                      dline_t** result,
                      char* string,
                      unsigned int start,
                      unsigned int total_len) {
  if(existing == NULL || result == NULL)
    return BAD_PARAM;
  
  dline_entry* current = (dline_entry*)existing;
  uint64_t before_size, deleted_size, after_size = 0;
  unsigned int suffix_len = total_len-start;
  
  /* Room for optimization here: after removing first suffix, we can save
   * the global_ptr, and then shallow compare the pointers when we delete
   * other suffixes.
   */
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
        (suffix_len != current->len ||
         strncmp(str_offset(current), string + start, suffix_len) ||
         current->global_ptr->len != total_len ||
         memcmp(GLOBAL_STR(current->global_ptr), string, total_len))) {
    current = next_entry(current);
  }
  
  if(current->global_ptr == DLINE_MAGIC_TERMINATOR) {
    assert(0); /*shouldn't happen, lets go cry in a corner for now*/
  }
  
  before_size = (uint64_t)current - (uint64_t)existing;
  deleted_size = entry_size(current->len);
  
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR) {
    current = next_entry(current);
  }
  
  after_size = (uint64_t)current - (uint64_t)existing - deleted_size -
    before_size;
  
  if(before_size == 0 && after_size == 0) {
    /* If the deleted suffix is the only entry, no need to malloc a new one.
     */
    *result = NULL;
    return NO_ERROR;
  }
  
  *result = (dline_t*)malloc(before_size + after_size + sizeof(void*));
  
  if(*result == NULL) {
    return MALLOC_FAIL;
  }
  
  /*copy over entries before the deleted suffix*/
  memcpy(*result, existing, before_size);
  
  /*and now the entries after*/
  memcpy(((char*)*result) + before_size,
         ((char*)existing) + before_size + deleted_size,
         after_size);
  
  /*termiante with magic (Ugh Ugly)*/
  ((dline_entry*)((char*)*result + before_size + after_size))->global_ptr = DLINE_MAGIC_TERMINATOR;
  
  return NO_ERROR;
}

/* Search the given dline for suffixes starting with string[start] and
 * minimum score of min_score. Stores result_size number of entries in
 * results, and returns the number of results stored there.
 */
int dline_search(dline_t* dline,
                 char* string,
                 unsigned int start,
                 unsigned int total_len,
                 int min_score,
                 dline_entry* results,
                 int result_len) {
  if(dline == NULL || string == NULL || results == NULL)
    return 0;
  
  dline_entry* current = (dline_entry*)dline;
  int num_found = 0;
  unsigned int match_len = total_len-start;
  
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
        current->score >= min_score) {
    if(match_len <= current->len &&
       !memcmp(string+start, str_offset(current), match_len)) {
      results[num_found].global_ptr = current->global_ptr;
      results[num_found].score = current->score;
      results[num_found].len = current->len;
      num_found++;
      if(num_found == result_len)
        break;
    }
    
    current = next_entry(current);
  }
  
  return num_found;
}

/* Simple debugging function which outputs all of the contents of a dline.
 */
void dline_debug(dline_t* dline) {
  dline_entry* current = (dline_entry*)dline;
  printf("dline at 0x%llx\n", (uint64_t)current);
  
  if(dline == NULL) {
    printf("pointer is null, no entries here\n");
    return;
  }
  
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR) {
    /* bs required to add a null terminator, else we would have to scanf
     * the printf string to add precision since the length isn't known in
     * advance (even uglier).
     */
    char* tmp = (char*)malloc(current->len+1);
    assert(tmp != NULL);
    strncpy(tmp, (char*)current + sizeof(dline_entry), current->len);
    tmp[current->len] = '\0';
    printf("ptr: %p\nlen: %u\nscr: %u\n[%s]\n", (void*)current->global_ptr,
                                                 current->len,
                                                 current->score,
                                                 tmp);
    free(tmp);
    current = next_entry(current);
  }
  printf("Total length: %llu\n", (uint64_t)current - (uint64_t)dline +
         sizeof(void*));
}