#ifndef _TRIE_H_
#define _TRIE_H_

#include "cobb2.h"
#include "dline.h"

#define NUM_BUCKETS 64

typedef void trie_t;

typedef struct trie_node {
  dline_t* terminated;
  void* cache_unused;
  trie_t* children[256]; /*store type in lowest bit*/
} trie_node;

typedef struct hash_node {
  int size;
  dline_t* entries[NUM_BUCKETS];
} hash_node;


op_result trie_upsert(trie_t* existing,
                      char* string,
                      unsigned int start,
                      unsigned int total_len,
                      int score,
                      upsert_state* state);

op_result trie_remove(trie_t* existing,
                      char* string,
                      unsigned int start,
                      unsigned int total_lem);
#endif
