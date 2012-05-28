#ifndef _SERVER_H_
#define _SERVER_H_

#include "trie.h"
#include "parse.h"

typedef struct server_state {
  parser_data parser;
  trie_t* trie;
} server_state;

#endif
