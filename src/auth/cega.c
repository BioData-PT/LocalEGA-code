#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <libpq-fe.h>

#include <curl/curl.h>
#include <json-c/json.h>

#include "debug.h"
#include "config.h"
#include "backend.h"

#define URL_SIZE 1024
#define EXPIRATION_INTERVAL ""

struct curl_res_s {
  char *body;
  size_t size;
};

/* callback for curl fetch */
size_t
curl_callback (void *contents, size_t size, size_t nmemb, void *p) {
  const size_t realsize = size * nmemb;                      /* calculate buffer size */
  struct curl_res_s *cres = (struct curl_res_s *) p;   /* cast pointer to fetch struct */

  /* expand buffer */
  cres->body = (char *) realloc(cres->body, cres->size + realsize + 1);

  /* check buffer */
  if (cres->body == NULL) {
    D("ERROR: Failed to expand buffer in curl_callback\n");
    /* free(p); */
    return -1;
  }

  /* copy contents to buffer */
  memcpy(&(cres->body[cres->size]), contents, realsize);
  cres->size += realsize;
  cres->body[cres->size] = '\0';

  return realsize;
}

bool
fetch_from_cega(const char *username, char **buffer, size_t *buflen, int *errnop)
{
  CURL *curl;
  CURLcode res;
  bool success = false;
  char endpoint[URL_SIZE];
  struct curl_res_s *cres = NULL;
  enum json_tokener_error jerr = json_tokener_success;
  json_object *pwdh = NULL, *pubkey = NULL, *expiration = NULL;
  json_object *json = NULL, *json_response = NULL, *json_result = NULL, *jobj = NULL;
  char* endpoint_creds = NULL;

  D("contacting cega for user: %s\n", username);

  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();

  if(!curl) { D("libcurl init failed\n"); goto BAIL_OUT; }

  if( !sprintf(endpoint, options->rest_endpoint, username )){
    D("Endpoint URL looks weird for user %s: %s\n", username, options->rest_endpoint);
    goto BAIL_OUT;
  }

  cres = (struct curl_res_s*)malloc(sizeof(struct curl_res_s));
  
 
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS    , 1L               ); /* shut off the progress meter */
  curl_easy_setopt(curl, CURLOPT_URL           , endpoint         );
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION , curl_callback    );
  curl_easy_setopt(curl, CURLOPT_WRITEDATA     , (void *)cres     );
  curl_easy_setopt(curl, CURLOPT_FAILONERROR   , 1L               ); /* when not 200 */

  curl_easy_setopt(curl, CURLOPT_HTTPAUTH      , CURLAUTH_BASIC);
  endpoint_creds = (char*)malloc(1 + strlen(options->rest_user) + strlen(options->rest_password));
  sprintf(endpoint_creds, "%s:%s", options->rest_user, options->rest_password);
  curl_easy_setopt(curl, CURLOPT_USERPWD       , endpoint_creds);
 
  /* curl_easy_setopt(curl, CURLOPT_SSLCERT      , options->ssl_cert); */
  /* curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE  , "PEM"            ); */

#ifdef DEBUG
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif

  /* Perform the request, res will get the return code */
  D("Connecting to %s\n", endpoint);
  res = curl_easy_perform(curl);
  if(res != CURLE_OK){
    D("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    goto BAIL_OUT;
  }

  json = json_tokener_parse_verbose(cres->body, &jerr);

  if (jerr != json_tokener_success) {
    D("Failed to parse json string\n");
    goto BAIL_OUT;
  }

  json_object_object_get_ex(json, "response", &json_response);
  json_object_object_get_ex(json_response, "result", &json_result);

  jobj = json_object_array_get_idx(json_result,0);
  json_object_object_get_ex(jobj, "password", &pwdh);
  json_object_object_get_ex(jobj, "public_key", &pubkey);
  json_object_object_get_ex(jobj, "expiration", &expiration);

  success = add_to_db(username,
		      json_object_get_string(pwdh),
		      json_object_get_string(pubkey),
		      json_object_get_string(expiration));

BAIL_OUT:
  if(!success) D("user %s not found\n", username);
  if(cres) free(cres);
  if(endpoint_creds) free(endpoint_creds);
  json_object_put(jobj);
  json_object_put(json_result);
  json_object_put(json_response);
  json_object_put(json);
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  return success;
}
