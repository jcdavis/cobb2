#include <assert.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include "dline.h"
#include "http.h"
#include "parse.h"
#include "trie.h"

#define NUM_RESULTS 25
static result_entry results[NUM_RESULTS];

void prefix_handler(struct evhttp_request *req, void* arg) {
  struct evbuffer* ret = evbuffer_new();
  struct evkeyvalq params;
  struct evkeyval* param;
  const char* uri = evhttp_request_get_uri(req);
  char* full_string = NULL;
  char* callback = NULL;

  assert(ret != NULL);
  TAILQ_INIT(&params);

  evhttp_parse_query(uri, &params);

  TAILQ_FOREACH(param, &params, next) {
    if(param->key != NULL && !strcmp(param->key, "q"))
      full_string = param->value;
    if(param->key != NULL && !strcmp(param->key, "callback"))
      callback = param->value;
  }
  if(full_string == NULL) {
    evhttp_send_error(req, 400, "Bad Syntax");
    evbuffer_free(ret);
    return;
  }

  /* This strlen is almost certainly a security bug */
  string_data string = {full_string, NULL, strlen(full_string)};
  assert(!normalize(full_string, string.length, &(string.normalized)));

  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "application/json");
  if(callback == NULL) {
    evbuffer_add_printf(ret, "{\"results\":[");
  } else {
    evbuffer_add_printf(ret, "%s({\"results\":[", callback);
  }
  int len = trie_search((trie_t*)arg, &string, results,
                        NUM_RESULTS);
  for(int i = 0; i < len; i++) {
    int total = results[i].global_ptr->len;
    int start = total-results[i].len-results[i].offset;
    char* encoded_string = evhttp_htmlescape(GLOBAL_STR(results[i].global_ptr));
    
    if(encoded_string == NULL) {
      evhttp_send_error(req, 500, "Server Error");
      evbuffer_free(ret);
      return;
    }
    
    evbuffer_add_printf(ret,
      "%s{\"str\":\"%s\",\"scr\":%d,\"st\":%d,\"len\":%d}",
      i == 0 ? "" : ",",
      encoded_string,
      (int)results[i].score,
      (int)start,
      (int)(string.length));
    free(encoded_string);
  }
  
  evbuffer_add_printf(ret, "]}%s\n", callback != NULL ? ")" : "");
  evhttp_send_reply(req, HTTP_OK, "OK", ret);
  
  evhttp_clear_headers(&params);
  free(string.normalized);
  evbuffer_free(ret);
}

void quit_handler(struct evhttp_request* req, void* arg) {
  printf("!!!!I was told to Quit!!!!\n");
  evhttp_send_reply(req, HTTP_OK, "OK", NULL);
  trie_clean((trie_t*)arg);
  exit(0);
}

void init_and_run(trie_t* trie, int port) {
  struct event_base* base;
  struct evhttp* http;

  base = event_base_new();
  assert(base != NULL);

  http = evhttp_new(base);
  assert(http != NULL);

  evhttp_set_cb(http, "/complete", prefix_handler, (void*)trie);
  evhttp_set_cb(http, "/admin/quit", quit_handler, (void*)trie);

  assert(evhttp_bind_socket_with_handle(http, "0.0.0.0", port) != NULL);
  event_base_dispatch(base);
  /*Point of no return, hopefully*/
}
