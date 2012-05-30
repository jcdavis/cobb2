#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include "cobb2.h"
#include "dline.h"
#include "http.h"
#include "parse.h"
#include "server.h"
#include "trie.h"

void file_trie_query(char* fname);
void basic_test();
void parser_test();

/* stolen from https://gist.github.com/1087739 */
static void get_time(struct timespec* ts) {
  #ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  ts->tv_sec = mts.tv_sec;
  ts->tv_nsec = mts.tv_nsec;
  #else
  clock_gettime(CLOCK_REALTIME, ts);
  #endif
}

int main(int argc, char** argv) {
  if(argc >= 2)
    file_trie_query(argv[1]);
  else
    file_trie_query(NULL);
  //basic_test();
  //parser_test();
}

void basic_test() {
  static char* string1 = "Hello World!";
  static char* string2 = "Foo Bar Baz";
  static char* string3 = "42";
  
  string_data stringdata1 = {string1, string1, strlen(string1)};
  string_data stringdata2 = {string2, string2, strlen(string2)};
  string_data stringdata3 = {string3, string3, strlen(string3)};
  upsert_state state1 = {NULL, 0, 0};
  upsert_state state2 = {NULL, 0, 0};
  upsert_state state3 = {NULL, 0, 0};
  dline_t* line1 = NULL;
  dline_t* line2 = NULL;
  
  assert(!dline_upsert(line1, &line2, &stringdata1, 2, 9000, &state1));
  assert(!dline_upsert(line2, &line1, &stringdata2, 6, 9002, &state2));
  free(line2);
  assert(!dline_upsert(line1, &line2, &stringdata3, 0, 9001, &state3));
  free(line1);
  //Insert a second suffix for string3 with just a single character
  assert(!dline_upsert(line2, &line1, &stringdata3, 1, 9001, &state3));
  free(line2);
  line2 = line1;
  
  dline_debug(line2);
  //move string2 down to the bottom
  state2.mode = UPSERT_MODE_INITIAL;
  state2.global_ptr = NULL;
  assert(!dline_upsert(line2, &line1, &stringdata2, 6, 42, &state2));
  free(line2);
  dline_debug(line1);
  //and now string1 up to the top
  state1.mode = UPSERT_MODE_INITIAL;
  state1.global_ptr = NULL;
  assert(!dline_upsert(line1, &line2, &stringdata1, 2, 9005, &state1));
  free(line1);
  dline_debug(line2);
  
  remove_state rstate = {NULL};
  assert(!dline_remove(line2, &line1, &stringdata2, 6, &rstate));
  assert(rstate.global_ptr == state2.global_ptr);
  free(rstate.global_ptr);
  free(line2);
  dline_debug(line1);
  
  rstate.global_ptr = NULL;
  assert(!dline_remove(line1, &line2, &stringdata1, 2, &rstate));
  assert(rstate.global_ptr == state1.global_ptr);
  free(rstate.global_ptr);
  free(line1);
  dline_debug(line2);
  
  rstate.global_ptr = NULL;
  assert(!dline_remove(line2, &line1, &stringdata3, 0, &rstate));
  assert(rstate.global_ptr == state3.global_ptr);
  free(line2);
  dline_debug(line1);
  //remove the second suffix of string3
  assert(!dline_remove(line1, &line2, &stringdata3, 1, &rstate));
  assert(rstate.global_ptr == state3.global_ptr);
  free(line1);
  dline_debug(line2);
  free(rstate.global_ptr);
}

void input_parse_state(parser_data* data) {
  assert(data != NULL);
  char sin[500], min[500];

  printf("start chars\n");
  fgets(sin, 500, stdin);
  sin[strlen(sin)-1] = '\0'; /*damn newline*/

  printf("middle chars\n");
  fgets(min, 500, stdin);
  min[strlen(min)-1] = '\0'; /*damn newline*/

  parser_data_init(data, sin, min);
}

void init_server(server_t* server) {
  input_parse_state(&server->parser);
  /* Magic numbers everywhere */
  server->trie = trie_presplit(32, 127, 2);
}

void file_trie_query(char* fname) {
  server_t server;

  init_server(&server);  

  if(fname != NULL) {
    FILE* fp = fopen(fname, "r");
    char iline[500]; /*please say this is enough*/
    int read = 0;
    while(fgets(iline, 5001, fp)) {
      iline[strlen(iline)-1] = '\0'; /*damn newline*/
      server_upsert(&server, iline, strlen(iline));
      
      if(read % 10000 == 0) {
        printf("finished %d\n", read);
      }
      read++;
    }

    fclose(fp);
    printf("read %d lines\n", read);
  }
#if 0
  char iline[500]; /*please say this is enough*/
  result_entry results[25];

  while(fgets(iline, 500, stdin)) {
    iline[strlen(iline)-1] = '\0'; /*damn newline*/
    struct timespec ts_before;
    struct timespec ts_after;
    get_time(&ts_before);
    string_data string;

    assert(!normalize(iline, &string));
    int num = server_search(&server, &string, results, 25);
    get_time(&ts_after);
    for(int i = 0; i < num; i++) {
      printf("%d %p %s\n", results[i].score, (void*)(results[i].global_ptr),
             GLOBAL_STR(results[i].global_ptr));
    }
    double ms_time = 1000*(ts_after.tv_sec-ts_before.tv_sec);
    /* In the event we hit a second boundary, this is negative, so subracts
     * off the time added from a second above
     */
    ms_time += ((double)(ts_after.tv_nsec-ts_before.tv_nsec))/(1000000.0f);
    printf("took %5.5g ms\n", ms_time);
    
  }
#endif
  init_and_run(&server, 5402);

}
