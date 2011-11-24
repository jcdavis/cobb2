#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include "dline.h"
#include "cobb2.h"
#include "trie.h"

#define IS_HASH_NODE(ptr) (((uint64_t)ptr)&1)

/* Because we are doing prefix matching, we can only hash based on the first
 * character
 */
static inline uint64_t hash_idx(char first) {
  return (uint64_t)first%NUM_BUCKETS;
}

/* Apply the upsert to this trie, returning the success/error
 */
op_result trie_upsert(trie_t* existing,
                      char* string,
                      unsigned int start,
                      unsigned int total_len,
                      int score,
                      upsert_state* state) {
  assert(existing != NULL);
  
  int current_start = start;
  trie_t* current_ptr = existing;
  trie_node* parent_ptr = NULL;
  
  while(current_start < total_len && current_ptr != NULL &&
        !IS_HASH_NODE(current_ptr)) {
    parent_ptr = (trie_node*)current_ptr;
    current_ptr = ((trie_node*)current_ptr)->children[(int)string[current_start]];
    current_start++;
  }
  
  if(current_start == total_len) {
    /*suffix terminates at this node*/
    dline_t* new_dline;
    op_result result = dline_upsert(((trie_node*)current_ptr)->terminated,
                                    &new_dline,
                                    string,
                                    current_start,
                                    total_len,
                                    score,
                                    state);
    if(result == NO_ERROR) {
      free(((trie_node*)current_ptr)->terminated);
      ((trie_node*)current_ptr)->terminated = new_dline;
    }
    
    return result;
  } else {
    /*inserting into a hash_node, create it if it doesn't exist*/
    hash_node* hash_ptr = (hash_node*)((uint64_t)current_ptr-1);
    if(hash_ptr == NULL) {
      hash_ptr = (hash_node*)malloc(sizeof(hash_node));
      if(hash_ptr == NULL) 
        return MALLOC_FAIL;
      
      /*set parent trie node to point to our new hash node*/
      parent_ptr->children[(int)string[current_start-1]] =
        (trie_t*)((uint64_t)hash_ptr+1);
    }
    
    uint64_t idx = hash_idx(string[current_start]);
    
    /*HUGE TODO: split*/
    dline_t* new_dline;
    
    op_result result = dline_upsert(hash_ptr->entries[idx],
                                    &new_dline,
                                    string,
                                    current_start,
                                    total_len,
                                    score,
                                    state);
    if(result == NO_ERROR) {
      free(hash_ptr->entries[idx]);
      hash_ptr->entries[idx] = new_dline;
      hash_ptr->size++;
    }
    return result;
  }
}

 /* Delete from this trie, returning success/error
  */
op_result trie_remove(trie_t* existing,
                      char* string,
                      unsigned int start,
                      unsigned int total_len) {
  assert(existing != NULL);
  
  int current_start = start;
  trie_t* current_ptr = existing;
  trie_node* parent_ptr = NULL;
  
  while(current_start < total_len && current_ptr != NULL &&
        !IS_HASH_NODE(current_ptr)) {
    parent_ptr = (trie_node*)current_ptr;
    current_ptr = ((trie_node*)current_ptr)->children[(int)string[current_start]];
    current_start++;
  }
  
  if(current_start == total_len) {
    /*suffix terminates at this node, delete from its terminated dline*/
    dline_t* new_dline;
    op_result result = dline_remove(((trie_node*)current_ptr)->terminated,
                                    &new_dline,
                                    string,
                                    current_start,
                                    total_len);
    if(result == NO_ERROR) {
      free(((trie_node*)current_ptr)->terminated);
      ((trie_node*)current_ptr)->terminated = new_dline;
    }
    
    return result;
  } else {
    /* Deleting from a hash_node. Note: we don't delete the hash_node
     * if it is empty, which could be optimized
     */
    hash_node* hash_ptr = (hash_node*)((uint64_t)current_ptr-1);
    if(current_ptr == NULL) {
      assert(0); /*Shouldn't be happening*/
    }
    
    uint64_t idx = hash_idx(string[current_start]);
    dline_t* new_dline;
    
    op_result result = dline_remove(hash_ptr->entries[idx],
                                    &new_dline,
                                    string,
                                    current_start,
                                    total_len);
    if(result == NO_ERROR) {
      free(hash_ptr->entries[idx]);
      hash_ptr->entries[idx] = new_dline;
      hash_ptr->size--;
    }
    return result;
  }
}
