#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dline.h"

/*static char* string1 = "Hello World!";
static char* string2 = "Foo Bar Baz";
static char* string3 = "42";*/

int main(int argc, char** argv) {
  if(argc != 2)
    return 1;
  
  FILE* fp = fopen(argv[1], "r");
  
  char iline[500];
  
  dline* wtf = NULL;
  dline* num2 = NULL;
  
  int read = 0;
  
  while(fgets(iline, 500, fp)) {
    char* global_ptr = NULL;
    int unused = 0;
    short mode = DLINE_UPSERT_MODE_INITIAL;
    
    iline[strlen(iline)-1] = '\0';
    wtf = dline_upsert(num2,
                              iline,
                              0,
                              strlen(iline),
                              &global_ptr,
                              (100000-strlen(iline)),
                              &mode,
                              &unused);
    free(num2);
    num2 = wtf;
    read++;
  }
  fclose(fp);
  
  printf("read %d lines.\n Query fun times\n", read);
  
  dline_entry results[25];
  
  while(fgets(iline, 500, stdin)) {
    int found = dline_search(wtf, iline, 0, strlen(iline)-1, 0, results, 25);
    for(int i = 0; i < found; i++) {
      printf("%d %s\n", results[i].score, (char*)results[i].global_ptr);
    }
  }
  
  return 0;
}

/*char* global_ptr = NULL;
 int old_score = 0;
 short mode = DLINE_UPSERT_MODE_INITIAL;
 dline* result1 = dline_upsert(NULL,
 string1,
 2,
 strlen(string1),
 &global_ptr,
 9001,
 &mode,
 &old_score);
 mode = DLINE_UPSERT_MODE_INITIAL;
 global_ptr = NULL;
 dline* result2 = dline_upsert(result1,
 string2,
 6,
 strlen(string2),
 &global_ptr,
 9002,
 &mode,
 &old_score);
 mode = DLINE_UPSERT_MODE_INITIAL;
 global_ptr = NULL;
 dline* result3 = dline_upsert(result2,
 string3,
 0,
 strlen(string3),
 &global_ptr,
 9000,
 &mode,
 &old_score);
 dline_debug(result3);*/