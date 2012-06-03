#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "cmalloc.h"
#include "cobb2.h"
#include "dline.h"

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
 * Suffixes are stored in score sorted order, within score by global_ptr, and
 * withing global_ptr by length.
 * In actual usage, dlines are immutable: all operations create the a copy of 
 * the a dline with the given update applied to them.
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

static global_data* create_global(string_data* string) {
  global_data* result = cmalloc(sizeof(global_data) + string->length + 1);
  if(result == NULL)
    return NULL;
  result->len = string->length;
  memcpy(GLOBAL_STR(result), string->full, string->length + 1);
  
  return result;
}

/* Apply some function to each element of a dline. Brought out here so
 * caller don't need to be aware of memory layout.
 */
void dline_iterate(dline_t* dline, void* state, dline_iter_fn function) {
  assert(dline != NULL);
  
  dline_entry* current = (dline_entry*)dline;
  
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR) {
    function(current, str_offset(current), state);
    current = next_entry(current);
  }
}

/* Creates a copy of the given dline with the insert/update applied. If the
 * existing line is NULL, creates a new one with just the single element.
 * Returns status code of the operation.
 */
op_result dline_upsert(dline_t* existing, /* dline to perform upset on*/
                       dline_t** result, /* resulting dline*/
                       string_data* string,/* string information */
                       unsigned int start,/*offset from start of string*/
                       unsigned int score, /*score to set*/
                       upsert_state* state) {/*set on first call*/
  if(string == NULL || result == NULL || state == NULL)
    return BAD_PARAM;
  
  unsigned int suffix_len =
    start >= string->length ? 0 : string->length - start;
  
  if(existing == NULL) {
    /*if the dline is NULL, we can't possibily be doing an update*/
    assert(state->mode != UPSERT_MODE_UPDATE);
    
    /* Create the new dline for this suffix plus space for the trailing
     * magic terminator pointer.
     */
    *result = (dline_t*)cmalloc(entry_size(suffix_len) + sizeof(void*));
    
    if(*result == NULL) {
      return MALLOC_FAIL;
    }
    
    if(state->global_ptr == NULL) {
      assert(state->mode == UPSERT_MODE_INITIAL);
      /*first suffix insert, so create the global_ptr*/
      state->global_ptr = create_global(string);
      
      if(state->global_ptr == NULL) {
        cfree(*result);
        return MALLOC_FAIL;
      }
    }
    
    dline_entry* new_entry = (dline_entry*)*result;
    
    /*set up our new dline_t*/
    new_entry->global_ptr = state->global_ptr;
    new_entry->score = score;
    new_entry->len = suffix_len;
    memcpy(str_offset(new_entry), string->normalized + start, suffix_len);
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
     * is unecessary, since finding an identical global string is enough.
     */
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
          (suffix_len != current->len ||
           memcmp(str_offset(current), string->normalized + start,
                  suffix_len) ||
           current->global_ptr->len != string->length ||
           memcmp(GLOBAL_STR(current->global_ptr), string->full,
                  string->length))){
      current = next_entry(current);        
    }
    
    if(current->global_ptr == DLINE_MAGIC_TERMINATOR) {
      /* We didn't find an entry, do an insert. Re-looping isn't the most
       * efficient way of doing things, but it'll work for now
       */
      state->mode = UPSERT_MODE_INSERT;
      return dline_upsert(existing, result, string, start, score, state);
    } else {
      /* ran into a identical suffix with a full string identical to ours,
       * so this is an update. Similar to above, this just recurses for now.
       */
      state->mode = UPSERT_MODE_UPDATE;
      state->global_ptr = current->global_ptr;
      state->old_score = current->score;
      return dline_upsert(existing, result, string, start, score, state);
    }
    
    return NO_ERROR;
    
  } else if(state->mode == UPSERT_MODE_INSERT) {
    /* Doing an insert, loop until the first item whose score is <= the new
     * suffix's score, and then copy the data before, insert the new suffix,
     * and copy the data after.
     */
    
    dline_entry* current = existing;
    uint64_t before_size, after_size = 0;
    
    if(state->global_ptr == NULL) {
      /*first suffix insert, so create the global_ptr*/
      state->global_ptr = create_global(string);
      
      if(state->global_ptr == NULL) {
        return MALLOC_FAIL;
      }
    }
    
    /* Sort order is
     * 1. score
     * 2. global_ptr within score (important for fast merging of multiple
     * matching suffixes from the same string since this gives us deduping
     * for very cheap)
     * 3. length within global_ptr (thus when multiple suffixes from the
     * same root string on this dline match, return just the longest one,
     * which therefore starts earliest in the string)
     */
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
          (current->score > score || (current->score == score &&
          (uint64_t)current->global_ptr > (uint64_t)state->global_ptr) ||
          (current->score == score &&
           (uint64_t)current->global_ptr == (uint64_t)state->global_ptr &&
           current->len > suffix_len))) {
      current = next_entry(current);
    }
    
    before_size = (uint64_t)current - (uint64_t)existing;
    
    while(current->global_ptr != DLINE_MAGIC_TERMINATOR) {
      current = next_entry(current);
    }
    
    after_size = (uint64_t)current - (uint64_t)existing - before_size;
    
    *result = (dline_t*)cmalloc(before_size + entry_size(suffix_len) +
                                after_size + sizeof(void*));
    
    if(*result == NULL) {
      cfree(state->global_ptr);
      return MALLOC_FAIL;
    }
    
    /*copy over entries before our new entry*/
    memcpy(*result, existing, before_size);
    
    dline_entry* new_entry = (dline_entry*)((char*)*result +
                                            before_size);
    
    /*set up our new dline_t*/
    new_entry->global_ptr = state->global_ptr;
    new_entry->score = score;
    new_entry->len = suffix_len;
    memcpy(str_offset(new_entry), string->normalized + start, suffix_len);
    
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
    
    /* This works, but its slow due to a double-copy. TODO: make it faster*/
    dline_t* tmp = NULL;
    remove_state r_state = {state->global_ptr};
    op_result update_result = dline_remove(existing,
                                           &tmp,
                                           string,
                                           start,
                                           &r_state);
    if(update_result == NO_ERROR) {
      /* For just this call, we are doing an insert (since the old entry
       * has been removed). Need to change back after the upsert call
       */
      state->mode = UPSERT_MODE_INSERT;
      update_result = dline_upsert(tmp,
                                   result,
                                   string,
                                   start,
                                   score,
                                   state);
      state->mode = UPSERT_MODE_UPDATE;
      /* slightly sketchy failure handling here, should be cleaned up */
      if(update_result != MALLOC_FAIL) {
        cfree(tmp);
      }
    }
    
    return update_result;
  } else {
    return BAD_PARAM;
  }
}

/* Removes a suffix from a dline by creating a copy without the removed
 * elements, and returns a result code
 */
op_result dline_remove(dline_t* existing,
                      dline_t** result,
                      string_data* string,
                      unsigned int start,
                      remove_state* state) {
  if(existing == NULL || result == NULL || string == NULL || state == NULL)
    return BAD_PARAM;
  
  dline_entry* current = (dline_entry*)existing;
  uint64_t before_size, deleted_size, after_size = 0;
  unsigned int suffix_len =
    start >= string->length ? 0 : string->length - start;

  
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
        ((state->global_ptr != NULL &&
          state->global_ptr != current->global_ptr) ||
        (suffix_len != current->len ||
         memcmp(str_offset(current), string->normalized + start,
                suffix_len) ||
         current->global_ptr->len != string->length ||
         memcmp(GLOBAL_STR(current->global_ptr), string->full,
                string->length)))) {
    current = next_entry(current);
  }
  
  if(current->global_ptr == DLINE_MAGIC_TERMINATOR) {
    return NOT_FOUND;
  }
  
  before_size = (uint64_t)current - (uint64_t)existing;
  deleted_size = entry_size(current->len);
  
  if(state->global_ptr == NULL)
    state->global_ptr = current->global_ptr;
  
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
  
  *result = (dline_t*)cmalloc(before_size + after_size + sizeof(void*));
  
  if(*result == NULL) {
    return MALLOC_FAIL;
  }
  
  /*copy over entries before the deleted suffix*/
  memcpy(*result, existing, before_size);
  
  /*and now the entries after*/
  memcpy(((char*)*result) + before_size,
         ((char*)existing) + before_size + deleted_size,
         after_size);
  
  /*terminate with magic (Ugh Ugly)*/
  ((dline_entry*)((char*)*result + before_size + after_size))->global_ptr =
    DLINE_MAGIC_TERMINATOR;
  
  return NO_ERROR;
}

/* Search the given dline for suffixes starting with string[start] and
 * minimum score of min_score. Stores at most result_len number of entries
 * in results, and returns the number of results stored there. Will NOT
 * return more than a single entry per global_ptr. If there are multiple
 * suffixes in this dline, it returns just the one with the longest length
 * (starting earliest in the string).
 */
int dline_search(dline_t* dline,
                 string_data* string,
                 unsigned int start,
                 unsigned int min_score,
                 result_entry* results,
                 int result_len) {
  if(dline == NULL || string == NULL || results == NULL)
    return 0;
  
  dline_entry* current = (dline_entry*)dline;
  int num_found = 0;
  unsigned int match_len =
    start >= string->length ? 0 : string->length - start;
  global_data* last_global_ptr = NULL;
  
  while(current->global_ptr != DLINE_MAGIC_TERMINATOR &&
        current->score >= min_score) {
    if(current->global_ptr != last_global_ptr &&
       match_len <= current->len &&
       !memcmp(string->normalized + start, str_offset(current), match_len)) {
      memcpy(&results[num_found], current, sizeof(dline_entry));
      results[num_found].offset = start;
      num_found++;
      if(num_found == result_len)
        break;
    }
    last_global_ptr = current->global_ptr;
    current = next_entry(current);
  }
  
  return num_found;
}

typedef struct dline_debug_state {
  uint64_t size;
  int print_contents;
} dline_debug_state;

/*Iterator function which prints out contents of a single entry in a dline*/
static void dline_debug_printer(dline_entry* entry,
                                char* string,
                                void* state) {
  /* bs required to add a null terminator, else we would have to scanf
   * the printf string to add precision since the length isn't known in
   * advance (even uglier).
   */
  dline_debug_state* debug_state = (dline_debug_state*)state;
  if(debug_state->print_contents) {
    char* tmp = (char*)cmalloc(entry->len+1);
    assert(tmp != NULL);
    strncpy(tmp, string, entry->len);
    tmp[entry->len] = '\0';
    printf("ptr: %p\nlen: %u\nscr: %u\n[%s]\n", (void*)entry->global_ptr,
           entry->len,
           entry->score,
           tmp);
    cfree(tmp);
  }
  debug_state->size += entry_size(entry->len);
}

/* Simple debugging function which outputs all of the contents of a dline.
 */
void dline_debug(dline_t* dline) {
  dline_entry* current = (dline_entry*)dline;
  dline_debug_state state = {0L,1};
  printf("dline at 0x%llx\n", (uint64_t)current);
  
  if(dline == NULL) {
    printf("pointer is null, no entries here\n");
    return;
  }
  
  dline_iterate(dline, &state, dline_debug_printer);
  printf("Total length: %llu\n", (state.size + 8));
}

/* return actual size of a dline in bytes
 */
uint64_t dline_size(dline_t* dline) {
  dline_debug_state state = {0L,0};
  if(dline == NULL) {
    return 0;
  }
  
  dline_iterate(dline, &state, dline_debug_printer);
  return state.size + 8;
}


/* Print debug information about a results array
 */
void result_entry_debug(result_entry* data, int size) {
  printf("for %d entries at %p\n", size, (void*)data);
  
  for(int i = 0; i < size; i++) {
    printf("Global %p score %d len %d offset %d\n",
           (void*)data[i].global_ptr,
           data[i].score,
           data[i].len,
           data[i].offset);
  }
}
