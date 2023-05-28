
#include <esp_http_server.h>
#include <esp_log.h>
#include <math.h>
#include <time.h>
#include "webserver.h"

/* These are in main.c */
extern struct ev evs[2];
extern int activeevs;

/* *******************************************************************
   ****** begin string definition, mostly for embedded webpages ******
   ******************************************************************* */

static const char startp[] = R"EOSTP(
<!DOCTYPE html>

<html><head><title>Foxis Mobile Weather station</title>
<link rel="stylesheet" type="text/css" href="/css">
</head><body>
<h1>Foxis Mobile WS</h1>
This is the debug interface for Foxis Mobile Weather station.<br>
<a href="/sensorshtml">Current sensor values as HTML table</a><br>
<a href="/json">Current sensor values as JSON</a><br>
<a href="/mobilestate">Mobile network state</a><br>
<br>
Log in to Admin Interface:<br>
<form action="/admin" method="POST">
Admin-Password:
<input type="text" name="updatepw">
<input type="hidden" name="action" value="overview">
<input type="submit" name="su" value="Execute Action">
</form>
</body></html>
)EOSTP";

static const char css[] = R"EOCSS(
body { background-color:#000000;color:#cccccc; }
table, th, td { border:1px solid #aaaaff;border-collapse:collapse;padding:5px; }
th { text-align:left; }
td { text-align:right; }
a:link, a:visited, a:hover { color:#ccccff; }
)EOCSS";

static const char senshtml_p1[] = R"EOSEHTP1(
<!DOCTYPE html>

<html><head><title>Foxis Mobile Weather station - last measured values</title>
<link rel="stylesheet" type="text/css" href="/css">
</head><body>
<h1>Foxis Mobile WS - last measurements</h1>
Please note that this page does not update automatically - you need to
hit the reload button in your browser.<br>
<table>
)EOSEHTP1";

static const char senshtml_p2[] = R"EOSEHTP2(
</table>
</body></html>
)EOSEHTP2";

/* *******************************************************************
   ****** end   string definition, mostly for embedded webpages ******
   ******************************************************************* */

esp_err_t get_startpage_handler(httpd_req_t * req) {
  /* This is just a static page linking to other pages and
   * containing a login form, hence this easy. */
  /* The following two lines are the default und thus redundant. */
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  httpd_resp_send(req, startp, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static httpd_uri_t uri_startpage = {
  .uri      = "/",
  .method   = HTTP_GET,
  .handler  = get_startpage_handler,
  .user_ctx = NULL
};

esp_err_t get_css_handler(httpd_req_t * req) {
  /* This is just a static page, hence this easy. */
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/css");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  httpd_resp_send(req, css, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static httpd_uri_t uri_getcss = {
  .uri      = "/css",
  .method   = HTTP_GET,
  .handler  = get_css_handler,
  .user_ctx = NULL
};

/* Helper function that prints one sensor into either
 * HTML table or JSON.
 * t = type (0 HTML, 1 JSON)
 * id = json id resp. html label
 * f = format string
 * v = value to print */
static char * printonefloatsensor(int t, char * pfp, char * id, char * f, float v)
{
  if (t == 0) { /* HTML */
    pfp += sprintf(pfp, "<tr><th>%s</th><td>", id);
    pfp += sprintf(pfp, f, v);
    pfp += sprintf(pfp, "</td></tr>");
  } else { /* JSON */
    pfp += sprintf(pfp, "\"%s\":\"", id);
    pfp += sprintf(pfp, f, v);
    pfp += sprintf(pfp, "\",");
  }
  return pfp;
}

static char * printallsensors(int t, char * pfp)
{
  int e = activeevs;
  if (t == 0) { /* HTML */
    pfp += sprintf(pfp, "<tr><th>ts</th><td>%lld</td></tr>", evs[e].lastupd);
  } else { /* JSON */
    pfp += sprintf(pfp, "\"ts\":\"%lld\",", evs[e].lastupd);
  }
  pfp = printonefloatsensor(t, pfp, "temp",      "%.2f", evs[e].temp);
  pfp = printonefloatsensor(t, pfp, "hum",       "%.1f", evs[e].hum);
  pfp = printonefloatsensor(t, pfp, "press",     "%.3f", evs[e].press);
  pfp = printonefloatsensor(t, pfp, "windspeed", "%.1f", evs[e].windspeed);
  pfp = printonefloatsensor(t, pfp, "winddir",   "%.1f", evs[e].winddirdeg);
  return pfp;
}

esp_err_t get_sensorshtml_handler(httpd_req_t * req)
{
  char myresponse[sizeof(senshtml_p1) + sizeof(senshtml_p2) + 2000];
  char * pfp; /* Pointer for (s)printf */
  strcpy(myresponse, senshtml_p1);
  pfp = myresponse + strlen(myresponse);
  pfp = printallsensors(0, pfp);
  strcat(myresponse, senshtml_p2);
  /* The following two lines are the default und thus redundant. */
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=29");
  httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static httpd_uri_t uri_getsensorshtml = {
  .uri      = "/sensorshtml",
  .method   = HTTP_GET,
  .handler  = get_sensorshtml_handler,
  .user_ctx = NULL
};

esp_err_t get_json_handler(httpd_req_t * req)
{
  char myresponse[2000];
  char * pfp; /* Pointer for (s)printf */
  strcpy(myresponse, "{");
  pfp = myresponse + strlen(myresponse);
  pfp = printallsensors(1, pfp);
  /* The last character will be a ',' that is to much - we overwrite
   * it with a "}" instead. */
  pfp--;
  strcpy(pfp, "}");
  /* The following two lines are the default und thus redundant. */
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/json");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=29");
  httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static httpd_uri_t uri_getjson = {
  .uri      = "/json",
  .method   = HTTP_GET,
  .handler  = get_json_handler,
  .user_ctx = NULL
};


void webserver_start(void)
{
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  config.server_port = 80;
  /* The default is undocumented, but seems to be only 4k. */
  config.stack_size = 10000;
  ESP_LOGI("webserver.c", "Starting webserver on port %d", config.server_port);
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE("webserver.c", "Failed to start HTTP server.");
    return;
  }
  httpd_register_uri_handler(server, &uri_startpage);
  httpd_register_uri_handler(server, &uri_getcss);
  httpd_register_uri_handler(server, &uri_getsensorshtml);
  httpd_register_uri_handler(server, &uri_getjson);
}

