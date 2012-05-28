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
#include "server.h"

#define NUM_RESULTS 25

static inline uint64_t json_replace(char c, char** escaped) {
  switch(c) {
    case '\b':
      *escaped = "\\b";
      return 2;
    case '\n':
      *escaped = "\\n";
      return 2;
    case '\r':
      *escaped = "\\r";
      return 2;
    case '\t':
      *escaped = "\\t";
      return 2;
    case '"':
      *escaped = "\\\"";
      return 2;
    case '\\':
      *escaped = "\\\\";
      return 2;
    case '/':
      *escaped = "\\/";
      return 2;
    default:
      return 1;
  }
}

/* JSON string escaping, styled in the way of libevent's http escaping*/
static char* json_escape(char* in) {
  uint64_t old_len = 0;
  uint64_t idx = 0;

  while(in[idx] != '\0') {
    char* unused = NULL;
    old_len += json_replace(in[idx], &unused);
    idx++;
  }

  char* buffer = malloc(old_len + 1);
  if(buffer == NULL)
    return NULL;

  char* p = buffer;
  for(int i = 0; i < idx; i++) {
    char* escaped = &in[i];
    uint64_t size = json_replace(in[i], &escaped);
    memcpy(p, escaped, size);
    p += size;
  }
  *p = '\0';

  return buffer;
}

void prefix_handler(struct evhttp_request *req, void* arg) {
  result_entry results[NUM_RESULTS];
  struct evbuffer* ret = evbuffer_new();
  struct evkeyvalq params;
  struct evkeyval* param;
  const char* uri = evhttp_request_get_uri(req);
  char* full_string = NULL;
  char* callback = NULL;

  assert(ret != NULL);
  TAILQ_INIT(&params);

  if(evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
    evhttp_send_error(req, 405, "must use GET for complete");
    evbuffer_free(ret);
    return;
  }

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
  string_data string;
  assert(!normalize(full_string, &string));

  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Content-Type", "application/json");
  if(callback == NULL) {
    evbuffer_add_printf(ret, "{\"results\":[");
  } else {
    evbuffer_add_printf(ret, "%s({\"results\":[", callback);
  }

  int len = server_search((server_t*)arg, &string, results, NUM_RESULTS);
  for(int i = 0; i < len; i++) {
    int total = results[i].global_ptr->len;
    int start = total-results[i].len-results[i].offset;
    char* encoded_string = json_escape(GLOBAL_STR(results[i].global_ptr));
    
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
  exit(0);
}

void init_and_run(server_t* server, int port) {
  struct event_base* base;
  struct evhttp* http;

  base = event_base_new();
  assert(base != NULL);

  http = evhttp_new(base);
  assert(http != NULL);

  evhttp_set_cb(http, "/complete", prefix_handler, (void*)server);
  evhttp_set_cb(http, "/admin/quit", quit_handler, (void*)server);

  assert(evhttp_bind_socket_with_handle(http, "0.0.0.0", port) != NULL);
  event_base_dispatch(base);
  /*Point of no return, hopefully*/
}
