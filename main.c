#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "cobb2.h"
#include "dline.h"
#include "trie.h"

void file_dline_query(char* fname);
void file_trie_query(char* fname);
void basic_test();

int main(int argc, char** argv) {
  file_trie_query(argv[1]);
  //basic_test();
}

void basic_test() {
  static char* string1 = "Hello World!";
  static char* string2 = "Foo Bar Baz";
  static char* string3 = "42";
  
  upsert_state state1 = {NULL, 0, 0};
  upsert_state state2 = {NULL, 0, 0};
  upsert_state state3 = {NULL, 0, 0};
  short mode = UPSERT_MODE_INITIAL;
  dline_t* line1 = NULL;
  dline_t* line2 = NULL;
  
  assert(!dline_upsert(line1, &line2, string1, 2, strlen(string1),
                       9000, &state1));
  mode = UPSERT_MODE_INITIAL;
  assert(!dline_upsert(line2, &line1, string2, 6, strlen(string2),
                       9002, &state2));
  free(line2);
  mode = UPSERT_MODE_INITIAL;
  assert(!dline_upsert(line1, &line2, string3, 0, strlen(string3),
                       9001, &state3));
  free(line1);
  dline_debug(line2);
  
  assert(!dline_remove(line2, &line1, string2, 6, strlen(string2)));
  free(line2);
  dline_debug(line1);
  assert(!dline_remove(line1, &line2, string1, 2, strlen(string1)));
  free(line1);
  dline_debug(line2);
  assert(!dline_remove(line2, &line1, string3, 0, strlen(string3)));
  free(line2);
  dline_debug(line1);
  
}

void file_trie_query(char* fname) {
  FILE* fp = fopen(fname, "r");
  char iline[500]; /*please say this is enough*/
  
  trie_t* trie = trie_init();
  
  int read = 0;
  
  while(fgets(iline, 500, fp)) {
    upsert_state state = {NULL,0,0};
    
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    assert(!trie_upsert(trie,
                        iline,
                        0,
                        strlen(iline),
                        (1000-strlen(iline)),
                        &state));
    read++;
  }
  fclose(fp);
  
  printf("read %d lines. Query:\n", read);
  
  dline_entry results[25];
  
  while(fgets(iline, 500, stdin)) {
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    int num = trie_search(trie, iline, strlen(iline), results, 25);
    for(int i = 0; i < num; i++) {
      printf("%d %s\n", results[i].score,
             GLOBAL_STR(results[i].global_ptr));
    }
  }
}

void file_dline_query(char* fname) {
  FILE* fp = fopen(fname, "r");
  char iline[500]; /*please say this is enough*/
  
  dline_t* line1 = NULL;
  dline_t* line2 = NULL;
  
  int read = 0;
  
  while(fgets(iline, 500, fp)) {
    upsert_state state = {NULL,0,0};
    
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    assert(!dline_upsert(line1,
                         &line2,
                         iline,
                         0,
                         strlen(iline),
                         (100000-strlen(iline)),
                         &state));
    free(line1);
    line1=line2;
    read++;
  }
  fclose(fp);
  
  printf("read %d lines. Query:\n", read);
  
  dline_entry results[25];
  
  while(fgets(iline, 500, stdin)) {
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    int num = dline_search(line1, iline, 0, strlen(iline), 0, results, 25);
    for(int i = 0; i < num; i++) {
      printf("%d %s\n", results[i].score,
                        GLOBAL_STR(results[i].global_ptr));
    }
  }
}