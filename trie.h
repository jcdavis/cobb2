#ifndef _TRIE_H_
#define _TRIE_H_

#include "cobb2.h"
#include "dline.h"

/*Constant for now*/
#define HASH_NODE_SIZE_LIMIT 10000

typedef void trie_t;

trie_t* trie_init();
void trie_clean(trie_t* trie);

op_result trie_upsert(trie_t* existing,
                      char* string,
                      unsigned int start,
                      unsigned int total_len,
                      int score,
                      upsert_state* state);

op_result trie_remove(trie_t* existing,
                      char* string,
                      unsigned int start,
                      unsigned int total_len);

int trie_search(trie_t* trie,
                char* string,
                unsigned int total_len,
                dline_entry* results,
                int results_len);
#endif
