#include <stdio.h>
#include <stdlib.h>
#include "cobb2.h"
#include "server.h"

/* Encapsulates operations on a server (which has a trie and parser)
 */

/* Upsert a string with score into the server.*/
op_result server_upsert(server_t* server,
                        char* input,/*assumed to have a trailing /0*/
                        unsigned int score) {
  if(server == NULL || input == NULL) {
    return BAD_PARAM;
  }
  
  string_data string;
  
  op_result res = normalize(input, &string);
  if(res != NO_ERROR)
    return res;
  
  int suffix_start = -1;
  upsert_state state = {NULL,0,0};
  
  while((suffix_start = next_start(&string,
                                   &server->parser,
                                   suffix_start)) >= 0) {
    res = trie_upsert(server->trie,
                      &string,
                      suffix_start,
                      string.length,
                      &state);
    if(res != NO_ERROR) {
      /* There is really not a clean way of recovering from this currently,
       * because updates are applied as they go. Realistically, the only way
       * this could happen would be if a malloc() failed, in which case we
       * are likely screwed anyways.
       * TODO: its possible that a trie_remove could undo enough to fix.
       */
      fprintf(stderr, "Failed mid-attempt update, be very afraid\n");
      return res;
    }
    
  }

  free(string.normalized);
  return NO_ERROR;
}

/* wrapper around trie_search */
int server_search(server_t* server,
                  string_data* string,/*leave normalize() out for now */
                  result_entry* results,
                  int results_len) {
  
  return trie_search(server->trie, string, results, results_len);
}
