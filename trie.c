#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dline.h"
#include "cobb2.h"
#include "trie.h"

/* Functions that operate on a trie. Every trie node has a list of suffixes
 * which terminate at it, as well as an array of children nodes for each byte
 * value. Children can either be other trie nodes, or hash table nodes, in
 * which there are a fixed number of buckets that the first unmatched byte
 * hashes to. In trie nodes, the lowest bit of each child pointer indicates
 * whether it points to a hash or trie node.
 */
#define NUM_BUCKETS 64
#define MIN(a,b) (a<b?a:b)

typedef struct trie_node {
  dline_t* terminated;
  trie_t* children[256]; /*store type (trie/hash) in lowest bit*/
} trie_node;

typedef struct hash_node {
  int size;
  dline_t* entries[NUM_BUCKETS];
} hash_node;

static inline uint64_t is_hash_node(trie_t* ptr) {
  return ((uint64_t)ptr)&1;
}

/* Because we are doing prefix matching, we can only hash based on the first
 * byte. But yea, this is still stupid
 */
static inline uint64_t hash_idx(char first) {
  return (uint64_t)first%NUM_BUCKETS;
}

trie_t* trie_init() {
  trie_node* node = (trie_node*)malloc(sizeof(trie_node));
  if(node == NULL)
    return NULL;
  node->terminated = NULL;
  
  for(int i = 0; i < 256; i++)
    node->children[i] = NULL;
  
  return (trie_t*)node;
}

/* Apply the upsert to this trie, returning the success/error
 */
op_result trie_upsert(trie_t* existing,
                      char* string,
                      unsigned int start,
                      unsigned int total_len,
                      int score,
                      upsert_state* state) {
  if(existing == NULL || string == NULL || state == NULL)
    return BAD_PARAM;
  
  int current_start = start;
  trie_t* current_ptr = existing;
  trie_node* parent_ptr = NULL;
  
  /* Loop down to either
   * 1) The trie node where this suffix terminates
   * 2) the hash node where this suffix goes (potentially terminating)
   * 3) an empty hash node where this suffix should go
   */
  while(current_start < total_len && current_ptr != NULL &&
        !is_hash_node(current_ptr)) {
    parent_ptr = (trie_node*)current_ptr;
    current_ptr =
      ((trie_node*)current_ptr)->children[(int)string[current_start]];
    current_start++;
  }
  
  if(current_ptr != NULL && !is_hash_node(current_ptr)) {
    /*suffix terminates at this trie node*/
    dline_t* new_dline;
    assert(!is_hash_node(current_ptr));
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
    if(current_ptr == NULL) {
      hash_ptr = (hash_node*)malloc(sizeof(hash_node));
      if(hash_ptr == NULL) 
        return MALLOC_FAIL;
      
      for(int i = 0; i < NUM_BUCKETS; i++) {
        hash_ptr->entries[i] = NULL;
      }
      
      /*set parent trie node to point to our new hash node*/
      parent_ptr->children[(int)string[current_start-1]] =
        (trie_t*)((uint64_t)hash_ptr+1);
    }
    
    /* If terminating, just hash to bucket 0. A terminating search
     * on this node will search across all buckets anyways, so it doesn't
     * particularly matter. However, it does mean searches on that bucket
     * for other prefixes may seek across unecessary terminating entries, so
     * it might be better to move those to a separate bucket.
     */
    uint64_t idx;
    if(current_start == total_len) {
      idx = 0;
    } else {
      idx = hash_idx(string[current_start]);
    }
    
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
  if(existing == NULL || string == NULL)
    return BAD_PARAM;
  
  int current_start = start;
  trie_t* current_ptr = existing;
  
  /*seek to the node we will remove from*/
  while(current_start < total_len && current_ptr != NULL &&
        !is_hash_node(current_ptr)) {
    current_ptr = ((trie_node*)current_ptr)->children[(int)string[current_start]];
    current_start++;
  }
  
  if(current_ptr == NULL) {
    return NOT_FOUND;
  } else if(!is_hash_node(current_ptr)) {
    /*suffix terminates at this trie node, delete from its terminated dline
     */
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
    
    uint64_t idx;
    if(current_start == total_len) {
      idx = 0;
    } else {
      idx = hash_idx(string[current_start]);
    }
    
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

/* TODO: can do a cute hack by unioning over score+len into a uint64_t/etc
 * and then doing a single movq instead of 2 movl :)
 */
static inline void copy_entry(dline_entry* dest, dline_entry* src) {
  dest->global_ptr = src->global_ptr;
  dest->score = src->score;
  dest->len = src->len;
}

/* Merge 2 sorted dline result lists, returning the number stored in dest.
 */
static int merge(dline_entry* s1,
                 int s1_num,
                 dline_entry* s2,
                 int s2_num,
                 dline_entry* dest,
                 int dest_len) {
  assert(s1 != NULL && s2 != NULL && dest != NULL);
  
  uint64_t s1_idx = 0, s2_idx = 0, dest_idx = 0;
  
  while(dest_idx < dest_len && s1_idx < s1_num && s2_idx < s2_num) {
    if(s1[s1_idx].score > s2[s2_idx].score ||
       (s1[s1_idx].score == s2[s2_idx].score &&
        (uint64_t)s1[s1_idx].global_ptr > (uint64_t)s2[s2_idx].global_ptr)) {
      copy_entry(&dest[dest_idx], &s1[s1_idx]);
      dest_idx++;
      s1_idx++;
    } else if(s2[s2_idx].score > s1[s1_idx].score ||
              (s2[s2_idx].score == s1[s1_idx].score &&
               ((uint64_t)s2[s2_idx].global_ptr >
                 (uint64_t)s1[s1_idx].global_ptr))) {
      copy_entry(&dest[dest_idx], &s2[s2_idx]);
      dest_idx++;
      s2_idx++;
    } else {
      /* s1.score=s2.score and s1.global_ptr=s2.global_ptr, so these are 2
       * different suffixes for the same entry. Save the result with the
       * longer length (ie, whose suffix starts earlier in the string)
       */
      if(s1[s1_idx].len > s2[s2_idx].len) {
        copy_entry(&dest[dest_idx], &s1[s1_idx]);
      } else {
        copy_entry(&dest[dest_idx], &s2[s2_idx]);
      }
      dest_idx++;
      s1_idx++;
      s2_idx++;
    }
  }
  
  /* If dest isn't full, check to see if one of the sources has extra
   * entries to copy
   */
  if(dest_idx < dest_len) {
    if(s1_idx < s1_num) {
      uint64_t smaller = MIN(dest_len-dest_idx,s1_num-s1_idx);
      memcpy(&dest[dest_idx], &s1[s1_idx],
             smaller*sizeof(dline_entry));
      dest_idx += smaller;
    } else {
      assert(s2_idx < s2_num || s2_num == 0);
      uint64_t smaller = MIN(dest_len-dest_idx,s2_num-s2_idx);
      memcpy(&dest[dest_idx], &s2[s2_idx],
             smaller*sizeof(dline_entry));
      dest_idx += smaller;
    }
  }
  
  return dest_idx;
}

/* Search the given hash node for suffixes starting with the given prefix.
 * Stores at most results_len results in to, returning the number stored.
 */
static int hash_node_search(hash_node* node,
                            char* string,
                            unsigned int start,
                            unsigned int total_len,
                            int min_score,
                            dline_entry* from,
                            dline_entry* to,
                            dline_entry* spare,
                            int from_size,
                            int results_len) {
  assert(node != NULL && string != NULL && from != NULL && to != NULL
         && spare != NULL);
  
  /* If there are any unmatched bytes, just search on the line the next byte
   * hashes to.
   */
  if(start < total_len) {
    uint64_t idx = hash_idx(string[start]);
    
    int build_size = dline_search(node->entries[idx],
                                  string,
                                  start,
                                  total_len,
                                  min_score,
                                  spare,
                                  results_len);
    return merge(from, from_size,
                 spare, build_size,
                 to, results_len);
  }
  
  /* Otherwise, prefix terminates at this node, so search across all lines.
   */
  
  int built_size = dline_search(node->entries[0],
                                string,
                                start,
                                total_len,
                                min_score,
                                to,
                                results_len);
  
  built_size = merge(to, built_size,
                     from, from_size,
                     spare, results_len);
  
  /* We alternate which buffer dline_search holds the results so far,
   * and then merge with the results from dline_search into the other buffer
   */
  dline_entry* built = spare;
  
  if(built_size == results_len) {
    min_score = built[results_len].score;
  }
  
  for(int i = 1; i < NUM_BUCKETS; i++) {
    /*for each bucket, get results & merge*/
    if(node->entries[i] != NULL) {
      int to_size = dline_search(node->entries[i],
                                 string,
                                 start,
                                 total_len,
                                 min_score,
                                 to,
                                 results_len);

      if(to_size > 0) {
        if(built == spare) {
          built_size = merge(to, to_size,
                             built, built_size,
                             from, results_len);
          built = from;
        } else {
          built_size = merge(to, to_size,
                            built, built_size,
                            spare, results_len);
          built = spare;
        }

        if(built_size == results_len) {
          min_score = built[built_size-1].score;
        }
      }
    }
  }
  
  /* This could be slightly optimized by writing to one of the buffers,
   * giving a 50% chance we don't have to do this copy. Or alternatively,
   * just rewriting which pointer has the results (or something of the sort)
   */
  memcpy(to, built, built_size*sizeof(dline_entry));
  return built_size;
}

/* Internal recursive trie search from the first node which could not
 * be seeked down further, meaning either
 * 1) The node is NULL
 * 2) The node is a hash node
 * 3) The node is a trie node either at *or below* where the string ends,
 * and thus all entries must be recursively returned.
 */
static int trie_fan_search(trie_t* trie,
                           char* string,
                           unsigned int start,
                           unsigned int total_len,
                           int min_score,
                           dline_entry* from,
                           dline_entry* to,
                           dline_entry* spare,
                           int from_size,
                           int results_len) {
  assert(trie != NULL && string != NULL && from != NULL && to != NULL
         && spare != NULL);
  if(is_hash_node(trie)) {
    return hash_node_search((hash_node*)((uint64_t)trie-1),
                            string,
                            start,
                            total_len,
                            min_score,
                            from,
                            to,
                            spare,
                            from_size,
                            results_len);
  } else {
    /*recurse over the terminators, and then every child*/
    trie_node* t_node = (trie_node*)trie;
    
    int built_size = dline_search(t_node->terminated,
                                  string,
                                  start,
                                  total_len,
                                  min_score,
                                  spare,
                                  results_len);
    
    built_size = merge(spare, built_size,
                       from, from_size,
                       to, results_len);
    
    dline_entry* old_results = to;
    dline_entry* new_results = from;
    
    if(built_size == results_len) {
      min_score = to[results_len].score;
    }
    
    for(int i = 0; i < 256; i++) {
      if(t_node->children[i] != NULL) {
        built_size = trie_fan_search(t_node->children[i],
                                     string,
                                     start+1,
                                     total_len,
                                     min_score,
                                     old_results,
                                     new_results,
                                     spare,
                                     built_size,
                                     results_len);
        if(built_size > 0) {
          if(old_results == from) {
            new_results = from;
            old_results = to;
          } else {
            new_results = to;
            old_results = from;
          }

          if(built_size == results_len) {
            min_score = old_results[built_size-1].score;
          }
        }
      }
    }
    
    if(new_results == from)
      memcpy(to, from, built_size*sizeof(dline_entry));
    return built_size;
  }
}

/* Search the given trie for suffixes starting with the given prefix.
 * Stores at most results_len results, and returns the number stored.
 */
int trie_search(trie_t* trie,
                char* string,
                unsigned int total_len,
                dline_entry* results,
                int results_len) {
  if(trie == NULL || string == NULL || results == NULL)
    return 0;
  
  int current_start = 0;
  trie_t* current_ptr = trie;
  
  /* first seek down to where we need to start collecting */
  while(current_start < total_len && current_ptr != NULL &&
        !is_hash_node(current_ptr)) {
    current_ptr = ((trie_node*)current_ptr)->children[(int)string[current_start]];
    current_start++;
  }
  
  if(current_ptr == NULL) {
    return 0;
  }
  
  dline_entry* spare1 = (dline_entry*)calloc(results_len,
                                             sizeof(dline_entry));
  if(spare1 == NULL) {
    return 0;
  }
  dline_entry* spare2 = (dline_entry*)calloc(results_len,
                                             sizeof(dline_entry));
  if(spare2 == NULL) {
    free(spare1);
    return 0;
  }

  int result = trie_fan_search(current_ptr,
                               string,
                               current_start,
                               total_len,
                               MIN_SCORE,
                               spare1,
                               results,
                               spare2,
                               0,
                               results_len);
  
  free(spare1);
  free(spare2);
  return result;
}

void hash_node_debug(trie_t* node) {
  if(node == NULL) {
    printf("hash? node is null\n");
    return;
  }
  
  hash_node* h_node = (hash_node*)node;
  printf("hash node at %p with %d elements\n", node,
                                               h_node->size);
  
  for(int i = 0; i < NUM_BUCKETS; i++) {
    if(h_node->entries[i] != NULL) {
      printf("%d: %p\n", i, h_node->entries[i]);
    }
  }
}

void trie_node_debug(trie_t* node) {
  if(node == NULL) {
    printf("trie node is null\n");
    return;
  }
  
  trie_node* t_node = (trie_node*)node;
  printf("hash node at %p\n", node);
  printf("terminated: %p\n", t_node->terminated);
  
  for(int i = 0; i < 256; i++) {
    if(t_node->children[i] != NULL) {
      printf("%d: %p\n", i, t_node->children[i]);
    }
  }
}

/* Depending on the type of node, either use the trie or hash debug function
 */
void trie_debug(trie_t* trie) {
  if(is_hash_node(trie)) {
    hash_node_debug((trie_t*)((uint64_t)trie-1));
  } else {
    trie_node_debug(trie);
  }
}
