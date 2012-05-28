#ifndef _SERVER_H_
#define _SERVER_H_

#include "cobb2.h"
#include "parse.h"
#include "trie.h"

typedef struct server_t {
  parser_data parser;
  trie_t* trie;
} server_t;

op_result server_upsert(server_t* server,
                        char* input,
                        unsigned int score);

int server_search(server_t* server,
                  string_data* string,
                  result_entry* results,
                  int results_len);

#endif
