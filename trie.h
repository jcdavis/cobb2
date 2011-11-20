#ifndef _TRIE_H_
#define _TRIE_H_

#include "dline.h"

#define NUM_BUCKETS 64

typedef void trie_t;

typedef struct trie_node {
  dline_t* terminated;
  trie_t* children[256]; /*store type in lowest bit*/
} trie_node;

typedef struct hash_node {
  int size;
  dline_t* entries[NUM_BUCKETS];
} hash_node;

#endif
