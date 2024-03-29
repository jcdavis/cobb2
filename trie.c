#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cmalloc.h"
#include "cobb2.h"
#include "dline.h"
#include "trie.h"

/* Functions that operate on a trie. Every trie node has a list of suffixes
 * which terminate at it, as well as an array of children nodes for each byte
 * value. Children can either be other trie nodes, or hash table nodes, in
 * which there are a fixed number of buckets that the first unmatched byte
 * hashes to. In trie nodes, the lowest bit of each child pointer indicates
 * whether it points to a hash or trie node.
 */
#define NUM_BUCKETS 63
#define MIN(a,b) (a<b?a:b)

typedef struct trie_node {
  dline_t* terminated;
  trie_t* children[256]; /*store type (trie/hash) in lowest bit*/
} trie_node;

typedef struct hash_node {
  int size;
  dline_t* entries[NUM_BUCKETS];
} hash_node;

typedef struct split_state {
  trie_t* new_node;
  op_result result;
} split_state;

static inline uint64_t is_hash_node(trie_t* ptr) {
  return ((uint64_t)ptr)&1;
}

static int trie_node_count = 0;
static int hash_node_count = 0;

/* Because we are doing prefix matching, we can only hash based on the first
 * byte. But yea, this is still stupid
 */
static inline uint64_t hash_idx(char first) {
  return (uint64_t)first%NUM_BUCKETS;
}

static void split_dline_iter_fn(dline_entry* entry,
                                char* normalized_string,
                                void* state) {
  split_state* spl_state = (split_state*)state;
  
  /* Iterator just loops over everything, so if there is a problem the rest
   * of of the dline will still be looped over, and we won't want to do
   * anything.
   */
  if(spl_state->result != NO_ERROR)
    return;
  
  /* Since this is re-inserting from a split, it is always an insert.
   * Starting offset is 0 because this is the suffix string stored in the
   * dline, not the full global string
   */
  upsert_state u_state = {entry->global_ptr, 0, UPSERT_MODE_INSERT};
  string_data string_data =
    {GLOBAL_STR(entry->global_ptr), normalized_string, entry->len};
  spl_state->result = trie_upsert(spl_state->new_node,
                                  &string_data,
                                  0,
                                  entry->score,
                                  &u_state);
}

trie_t* trie_init() {
  trie_node* node = (trie_node*)cmalloc(sizeof(trie_node));
  if(node == NULL)
    return NULL;
  node->terminated = NULL;
  
  for(int i = 0; i < 256; i++)
    node->children[i] = NULL;
  
  trie_node_count++;

  return (trie_t*)node;
}

/* Creates a trie with trie nodes pre-created in the low to high (inclusive)
 * byte ranges with a given depth. This reduces time doing unnecessary
 * splits in the case where a large amount of inserts are expected
 */
trie_t* trie_presplit(unsigned char low,
                      unsigned char high,
                      int depth) {
  trie_t* node = trie_init();
  if(node == NULL || depth <= 0)
    return node;
  for(unsigned char i = low; i <= high; i++) {
    trie_t* result = trie_presplit(low, high, depth - 1);
    if(result == NULL) {
      trie_clean(node);
      return NULL;
    }
    ((trie_node*)node)->children[i] = result;
  }
  return node;
}




/* Recursively free up a trie. Big TODO: this doesn't clean out the
 * relevant global pointers, hence is a leak if used for deleteing a whole
 * trie.
 */
void trie_clean(trie_t* trie) {
  assert(trie != NULL);
  if(is_hash_node(trie)) {
    hash_node* hash_ptr = (hash_node*)((uint64_t)trie-1);
    for(int i = 0; i < NUM_BUCKETS; i++) {
      if(hash_ptr->entries[i] != NULL)
        cfree(hash_ptr->entries[i]);
    }
    cfree(hash_ptr);
    hash_node_count--;
  } else {
    trie_node* trie_ptr = (trie_node*)trie;
    for(int i = 0; i < 256; i++) {
      if(trie_ptr->children[i] != NULL)
        trie_clean(trie_ptr->children[i]);
    }
    if(trie_ptr->terminated != NULL)
      cfree(trie_ptr->terminated);
    cfree(trie);
    trie_node_count--;
  }
}

/* Apply the upsert to this trie, returning the success/error
 */
op_result trie_upsert(trie_t* existing,
                      string_data* string,
                      unsigned int start,
                      unsigned int score,
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
  while(current_start < string->length && current_ptr != NULL &&
        !is_hash_node(current_ptr)) {
    parent_ptr = (trie_node*)current_ptr;
    current_ptr =
      parent_ptr->children[(int)(string->normalized[current_start])];
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
                                    score,
                                    state);
    if(result == NO_ERROR) {
      cfree(((trie_node*)current_ptr)->terminated);
      ((trie_node*)current_ptr)->terminated = new_dline;
    }
    
    return result;
  } else {
    /*inserting into a hash_node, create it if it doesn't exist*/
    hash_node* hash_ptr = (hash_node*)((uint64_t)current_ptr-1);
    if(current_ptr == NULL) {
      hash_ptr = (hash_node*)cmalloc(sizeof(hash_node));
      if(hash_ptr == NULL) 
        return MALLOC_FAIL;
      
      hash_node_count++;
      hash_ptr->size = 0;
      for(int i = 0; i < NUM_BUCKETS; i++) {
        hash_ptr->entries[i] = NULL;
      }
      
      /*set parent trie node to point to our new hash node*/
      parent_ptr->children[(int)(string->normalized[current_start-1])] =
        (trie_t*)((uint64_t)hash_ptr+1);
    } else if(state->mode != UPSERT_MODE_UPDATE &&
              hash_ptr->size >= HASH_NODE_SIZE_LIMIT) {
      /* Time to split the current hash node into a trie node with any
       * number of hash node children.
       * NOTE: since we do this before a dline_upsert call, its possible
       * that an update causes a split, because before the first suffix's
       * upsert the state->mode will still be INITIAL at this point.
       * TODO1: this is pretty inefficient, because we know in advance we
       * will be doing large numbers of inserts, thus a dline in a single
       * hash child will likely (but not necessarily, depending on
       * distribution), get multiple inserts, so a continual doubling-style
       * allocation is probably more appropriate than doing a perfect fit
       * malloc on every dline_upsert call here.
       * TODO2: we also probably will want to have the ability to limit the
       * number of splits done in a whole insert to some fixed number, most
       * likely 1, in order to avoid a slow worst case insert which happens
       * to be unlucky enough to have to split multiple hash nodes.
       */
      trie_node* trie_ptr = (trie_node*)trie_init();
      split_state spl_state = {trie_ptr, NO_ERROR};
      
      if(trie_ptr == NULL)
        return MALLOC_FAIL;
      
      /* Loop over hash entries and re-insert into a new trie node */
      for(int i = 0; i < NUM_BUCKETS && spl_state.result == NO_ERROR; i++) {
        if(hash_ptr->entries[i] != NULL) {
          dline_iterate(hash_ptr->entries[i], &spl_state,
                        split_dline_iter_fn);
        }
      }
      
      if(spl_state.result != NO_ERROR) {
        trie_clean(trie_ptr);
        return spl_state.result;
      }
      
      /*set parent trie node to point to our newly split trie node*/
      parent_ptr->children[(int)(string->normalized[current_start-1])] =
        (trie_t*)trie_ptr;
      
      /*recursively free up the old hash node*/
      trie_clean(current_ptr);
      
      /* Now do the actual upsert we came here to do, which may not still
       * insert onto a hash node (could have terminated at the hash node,
       * so it will now terminate at the newly split trie node)
       */
      return trie_upsert((trie_t*)trie_ptr,
                         string,
                         current_start,
                         score,
                         state);
    }
    
    /* If terminating, just hash to bucket 0. A terminating search
     * on this node will search across all buckets anyways, so it doesn't
     * particularly matter. However, it does mean searches on that bucket
     * for other prefixes may seek across unecessary terminating entries, so
     * it might be better to move those to a separate bucket.
     */
    uint64_t idx;
    if(current_start == string->length) {
      idx = 0;
    } else {
      idx = hash_idx(string->normalized[current_start]);
    }
    
    dline_t* new_dline;
    op_result result = dline_upsert(hash_ptr->entries[idx],
                                    &new_dline,
                                    string,
                                    current_start,
                                    score,
                                    state);
    if(result == NO_ERROR) {
      cfree(hash_ptr->entries[idx]);
      hash_ptr->entries[idx] = new_dline;
      
      if(state->mode != UPSERT_MODE_UPDATE)
        hash_ptr->size++;
    }
    return result;
  }
}

 /* Delete from this trie, returning success/error. Caller must free the
  * global_pointer in state after the last suffix removal
  */
op_result trie_remove(trie_t* existing,
                      string_data* string,
                      unsigned int start,
                      remove_state* state) {
  if(existing == NULL || string == NULL || state == NULL)
    return BAD_PARAM;
  
  int current_start = start;
  trie_t* current_ptr = existing;
  
  /*seek to the node we will remove from*/
  while(current_start < string->length && current_ptr != NULL &&
        !is_hash_node(current_ptr)) {
    current_ptr = ((trie_node*)current_ptr)->children[
      (int)(string->normalized[current_start])];
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
                                    state);
    if(result == NO_ERROR) {
      cfree(((trie_node*)current_ptr)->terminated);
      ((trie_node*)current_ptr)->terminated = new_dline;
    }
    
    return result;
  } else {
    /* Deleting from a hash_node. Note: we don't delete the hash_node
     * if it is empty, which could be optimized
     */
    hash_node* hash_ptr = (hash_node*)((uint64_t)current_ptr-1);
    
    uint64_t idx;
    if(current_start == string->length) {
      idx = 0;
    } else {
      idx = hash_idx(string->normalized[current_start]);
    }
    
    dline_t* new_dline;
    op_result result = dline_remove(hash_ptr->entries[idx],
                                    &new_dline,
                                    string,
                                    current_start,
                                    state);
    if(result == NO_ERROR) {
      cfree(hash_ptr->entries[idx]);
      hash_ptr->entries[idx] = new_dline;
      hash_ptr->size--;
    }
    return result;
  }
}

static inline void copy_entry(result_entry* restrict dest,
                              result_entry* restrict src) {
  memcpy(dest, src, sizeof(result_entry));
}

/* Merge 2 sorted dline result lists, returning the number stored in dest.
 */
static int merge(result_entry* restrict s1,
                 int s1_num,
                 result_entry* restrict s2,
                 int s2_num,
                 result_entry* restrict dest,
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
             smaller*sizeof(result_entry));
      dest_idx += smaller;
    } else if (s2_idx < s2_num) {
      uint64_t smaller = MIN(dest_len-dest_idx,s2_num-s2_idx);
      memcpy(&dest[dest_idx], &s2[s2_idx],
             smaller*sizeof(result_entry));
      dest_idx += smaller;
    }
  }
  
  return dest_idx;
}

/* Search the given hash node for suffixes starting with the given prefix.
 * Stores at most results_len results in to, returning the number stored.
 */
static int hash_node_search(hash_node* node,
                            string_data* string,
                            unsigned int start,
                            unsigned int min_score,
                            result_entry* from,
                            result_entry* to,
                            result_entry* spare,
                            int from_size,
                            int results_len) {
  assert(node != NULL && string != NULL && from != NULL && to != NULL
         && spare != NULL);
  
  /* If there are any unmatched bytes, just search on the line the next byte
   * hashes to.
   */
  if(start < string->length) {
    uint64_t idx = hash_idx(string->normalized[start]);
    
    int build_size = dline_search(node->entries[idx],
                                  string,
                                  start,
                                  min_score,
                                  spare,
                                  results_len);
    return merge(from, from_size,
                 spare, build_size,
                 to, results_len);
  }
  
  /* Otherwise, prefix terminates at this node, so search across all lines.
   * We alternate which buffer dline_search holds the results so far,
   * and then merge with the results from dline_search into the other buffer
   */
  result_entry* built = from;
  int built_size = from_size;
  
  for(int i = 0; i < NUM_BUCKETS; i++) {
    /*for each bucket, get results & merge*/
    if(node->entries[i] != NULL) {
      int to_size = dline_search(node->entries[i],
                                 string,
                                 start,
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
  memcpy(to, built, built_size*sizeof(result_entry));
  return built_size;
}

/* Internal recursive trie search from the first node which could not
 * be seeked down further, meaning either
 * 1) The node is a hash node
 * 2) The node is a trie node either at *or below* where the string ends,
 * and thus all entries must be recursively returned.
 */
static int trie_fan_search(trie_t* trie,
                           string_data* string,
                           unsigned int start,
                           unsigned int min_score,
                           result_entry* from,
                           result_entry* to,
                           result_entry* spare,
                           int from_size,
                           int results_len) {
  assert(trie != NULL && string != NULL && from != NULL && to != NULL
         && spare != NULL);
  if(is_hash_node(trie)) {
    return hash_node_search((hash_node*)((uint64_t)trie-1),
                            string,
                            start,
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
                                  min_score,
                                  spare,
                                  results_len);
    
    built_size = merge(spare, built_size,
                       from, from_size,
                       to, results_len);
    
    result_entry* old_results = from;
    result_entry* new_results = to;
    
    if(built_size == results_len) {
      min_score = to[results_len-1].score;
    }
    
    for(int i = 0; i < 256; i++) {
      if(t_node->children[i] != NULL) {
        if(old_results == from) {
          new_results = from;
          old_results = to;
        } else {
          new_results = to;
          old_results = from;
        }
        built_size = trie_fan_search(t_node->children[i],
                                     string,
                                     start+1,
                                     min_score,
                                     old_results,
                                     new_results,
                                     spare,
                                     built_size,
                                     results_len);
        if(built_size == results_len) {
          min_score = new_results[built_size-1].score;
        }
      }
    }
    
    if(new_results == from)
      memcpy(to, from, built_size*sizeof(result_entry));
    return built_size;
  }
}

/* Search the given trie for suffixes starting with the given prefix.
 * Stores at most results_len results, and returns the number stored.
 */
int trie_search(trie_t* trie,
                string_data* string,
                result_entry* results,
                int results_len) {
  if(trie == NULL || string == NULL || results == NULL)
    return 0;
  
  int current_start = 0;
  trie_t* current_ptr = trie;
  
  /* first seek down to where we need to start collecting */
  while(current_start < string->length && current_ptr != NULL &&
        !is_hash_node(current_ptr)) {
    current_ptr = ((trie_node*)current_ptr)->children[
      (int)(string->normalized[current_start])];
    current_start++;
  }
  
  if(current_ptr == NULL) {
    return 0;
  }
  
  result_entry* spare = (result_entry*)ccalloc(2*results_len,
                                               sizeof(result_entry));
  if(spare == NULL) {
    return 0;
  }

  int result = trie_fan_search(current_ptr,
                               string,
                               current_start,
                               MIN_SCORE,
                               spare,
                               results,
                               &spare[results_len],
                               0,
                               results_len);
  
  cfree(spare);
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
  printf("trie node at %p\n", node);
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

void trie_print_stats() {
  printf("%d trie nodes\n", trie_node_count);
  printf("%d hash nodes\n", hash_node_count);
}

/* Calculate how how much real memory is used by a given trie.
 * FAIL: this doesn't count global pointers!!!
 */
uint64_t trie_memory_usage(trie_t* trie) {
  uint64_t count = 0;
  
  if(is_hash_node(trie)) {
    count += sizeof(hash_node);
    hash_node* h_node = (hash_node*)((uint64_t)trie-1);
    
    for(int i = 0; i < NUM_BUCKETS; i++) {
      if(h_node->entries[i] != NULL) {
        count += dline_size(h_node->entries[i]);
      }
    }
  } else {
    count += sizeof(trie_node);
    trie_node* t_node = (trie_node*)trie;
    if(t_node->terminated != NULL) {
      count += dline_size(t_node->terminated);
    }
    
    for(int i = 0; i < 256; i++) {
      if(t_node->children[i] != NULL) {
        count += trie_memory_usage((trie_t*)(t_node->children[i]));
      }
    }
  }
  
  return count;
}
