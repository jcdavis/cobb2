#ifndef _TRIE_H_
#define _TRIE_H_

#include "cobb2.h"
#include "dline.h"

/*Constant for now*/
#define HASH_NODE_SIZE_LIMIT 15000

typedef void trie_t;

trie_t* trie_init();
trie_t* trie_presplit(unsigned char low,
                      unsigned char high,
                      int depth);
void trie_clean(trie_t* trie);

op_result trie_upsert(trie_t* existing,
                      string_data* string,
                      unsigned int start,
                      int score,
                      upsert_state* state);

op_result trie_remove(trie_t* existing,
                      string_data* string,
                      unsigned int start,
                      remove_state* state);

int trie_search(trie_t* trie,
                string_data* string,
                result_entry* results,
                int results_len);

uint64_t trie_memory_usage(trie_t* trie);

#endif
