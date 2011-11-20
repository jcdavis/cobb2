#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "dline.h"

void file_query(char* fname);

int main(int argc, char** argv) {
  file_query(argv[1]);
}

void basic_test() {
  static char* string1 = "Hello World!";
  static char* string2 = "Foo Bar Baz";
  static char* string3 = "42";
  
  char *global_ptr1 = NULL;
  char *global_ptr2 = NULL;
  char *global_ptr3 = NULL;
  int old_score = 0;
  short mode = DLINE_UPSERT_MODE_INITIAL;
  dline_t* line1 = NULL;
  dline_t* line2 = NULL;
  
  assert(!dline_upsert(line1, &line2, string1, 2, strlen(string1),
                       &global_ptr1, 9000, &mode, &old_score));
  mode = DLINE_UPSERT_MODE_INITIAL;
  assert(!dline_upsert(line2, &line1, string2, 6, strlen(string2),
                       &global_ptr2, 9002, &mode, &old_score));
  free(line2);
  mode = DLINE_UPSERT_MODE_INITIAL;
  assert(!dline_upsert(line1, &line2, string3, 0, strlen(string3),
                       &global_ptr3, 9001, &mode, &old_score));
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
  
  free(global_ptr1);
  free(global_ptr2);
  free(global_ptr3);
}

void file_query(char* fname) {
  FILE* fp = fopen(fname, "r");
  char iline[500]; /*please say this is enough*/
  
  dline_t* line1 = NULL;
  dline_t* line2 = NULL;
  
  int read = 0;
  
  while(fgets(iline, 500, fp)) {
    char* global_ptr = NULL;
    int unused = 0;
    short mode = DLINE_UPSERT_MODE_INITIAL;
    
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    assert(!dline_upsert(line1,
                         &line2,
                         iline,
                         0,
                         strlen(iline),
                         &global_ptr,
                         (100000-strlen(iline)),
                         &mode,
                         &unused));
    free(line1);
    line1=line2;
    read++;
  }
  fclose(fp);
  
  printf("read %d lines.\n Query:\n", read);
  
  dline_entry results[25];
  
  while(fgets(iline, 500, stdin)) {
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    int num = dline_search(line1, iline, 0, strlen(iline), 0, results, 25);
    for(int i = 0; i < num; i++) {
      printf("%d %s\n", results[i].score, (char*)results[i].global_ptr);
    }
  }
}