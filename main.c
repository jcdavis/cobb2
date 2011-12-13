#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <evhttp.h>
#include "cobb2.h"
#include "dline.h"
#include "parse.h"
#include "trie.h"

void file_dline_query(char* fname);
void file_trie_query(char* fname);
void basic_test();
void parser_test();

int main(int argc, char** argv) {
  file_trie_query(argv[1]);
  //basic_test();
  //parser_test();
}

void parser_test() {
  char iline[500];
  while(fgets(iline, 500, stdin)) {
    char* new;
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    assert(!normalize(iline, strlen(iline), &new));
    
    /*We don't want to match on the normalized input*/
    char* original = malloc(strlen(iline)+1);
    strncpy(original, iline, strlen(iline)+1);
    
    printf("start chars\n");
    fgets(iline, 500, stdin);
    unsigned char start_map[32];
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    bit_map_init(start_map, iline);
    
    printf("middle chars\n");
    fgets(iline, 500, stdin);
    unsigned char mid_map[32];
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    bit_map_init(mid_map, iline);
    
    int suffix_start = -1;
    while((suffix_start = next_start(original, strlen(new),
                                     start_map, mid_map,
                                     suffix_start)) >= 0) {
      printf("%s\n", new+suffix_start);
    }
    free(new);
  }
}

void basic_test() {
  static char* string1 = "Hello World!";
  static char* string2 = "Foo Bar Baz";
  static char* string3 = "42";
  
  upsert_state state1 = {NULL, 0, 0};
  upsert_state state2 = {NULL, 0, 0};
  upsert_state state3 = {NULL, 0, 0};
  dline_t* line1 = NULL;
  dline_t* line2 = NULL;
  
  assert(!dline_upsert(line1, &line2, string1, 2, strlen(string1),
                       9000, &state1));
  assert(!dline_upsert(line2, &line1, string2, 6, strlen(string2),
                       9002, &state2));
  free(line2);
  assert(!dline_upsert(line1, &line2, string3, 0, strlen(string3),
                       9001, &state3));
  free(line1);
  dline_debug(line2);
  //move string2 down to the bottom
  state2.mode = UPSERT_MODE_INITIAL;
  state2.global_ptr = NULL;
  assert(!dline_upsert(line2, &line1, string2, 6, strlen(string2),
                       42, &state2));
  free(line2);
  dline_debug(line1);
  //and now string1 up to the top
  state1.mode = UPSERT_MODE_INITIAL;
  state1.global_ptr = NULL;
  assert(!dline_upsert(line1, &line2, string1, 2, strlen(string1),
                       9005, &state1));
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
  char* new;
  
  printf("start chars\n");
  fgets(iline, 500, stdin);
  unsigned char start_map[32];
  iline[strlen(iline)-1] = '\0'; /*damn newline*/
  bit_map_init(start_map, iline);
  
  printf("middle chars\n");
  fgets(iline, 500, stdin);
  unsigned char mid_map[32];
  iline[strlen(iline)-1] = '\0'; /*damn newline*/
  bit_map_init(mid_map, iline);
  
  while(fgets(iline, 500, fp)) {
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    assert(!normalize(iline, strlen(iline), &new));

    int suffix_start = -1;
    upsert_state state = {NULL,0,0};
    while((suffix_start = next_start(iline, strlen(iline),
                                     start_map, mid_map,
                                     suffix_start)) >= 0) {
      assert(!trie_upsert(trie,
                          new,
                          suffix_start,
                          strlen(new),
                          strlen(new),
                          &state));
    }
    free(new);
    read++;
    
    if(read % 10000 == 0) {
      printf("finished %d\n", read);
    }
  }
  fclose(fp);
  
  printf("read %d lines. Query:\n", read);
  
  dline_entry results[25];
  
  while(fgets(iline, 500, stdin)) {
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    int num = trie_search(trie, iline, strlen(iline), results, 25);
    for(int i = 0; i < num; i++) {
      printf("%d %p %s\n", results[i].score, (void*)(results[i].global_ptr),
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
      printf("%d %p %s\n", results[i].score, (void*)(results[i].global_ptr),
                        GLOBAL_STR(results[i].global_ptr));
    }
  }
}