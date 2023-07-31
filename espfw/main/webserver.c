
#include <esp_http_server.h>
#include <esp_log.h>
#include <math.h>
#include <time.h>
#include "webserver.h"
#include "mobilenet.h"
#include "secrets.h"

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
<form action="/adminmenu" method="POST">
Admin-Password:
<input type="password" name="adminpw">
<input type="submit" name="su" value="Log In">
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

static const char mobstahtml_p1[] = R"EOMSHTP1(
<!DOCTYPE html>

<html><head><title>Foxis Mobile Weather station - mobile network state</title>
<link rel="stylesheet" type="text/css" href="/css">
</head><body>
<h1>Foxis Mobile WS - mobile state at last send interval</h1>
Please note that this page does not update automatically - you need to
hit the reload button in your browser.<br>
)EOMSHTP1";

static const char mobstahtml_p2[] = R"EOMSHTP2(
</body></html>
)EOMSHTP2";

static const char admmenhtml_p1[] = R"EOADMMHTP1(
<!DOCTYPE html>

<html><head><title>Foxis Mobile Weather station - admin menu</title>
<link rel="stylesheet" type="text/css" href="/css">
</head><body>
<h1>Foxis Mobile WS - admin menu</h1>
Welcome, Admin. Here are some things you can do:<br>
<h2>Manually select a network</h2>
<form action="/adminqueuecmd" method="POST">
)EOADMMHTP1";

static const char admmenhtml_p2[] = R"EOADMMHTP2(
<input type="hidden" name="mode" value="opsel">
<select name="operator">
<option value="NULL">Reset to automatic operator selection</option>
<option value="26201">Telekom DE</option>
<option value="26202">Vodafone DE</option>
<option value="26203">Telefonica DE</option>
</select>
<input type="checkbox" name="permanent" value="1"> permanent
<input type="submit" name="su" value="Execute">
</form>
</body></html>
)EOADMMHTP2";

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
    pfp += sprintf(pfp, "<tr><th>ts</th><td>%lld (%lld seconds ago)</td></tr>",
                        evs[e].lastupd, (time(NULL) - evs[e].lastupd));
  } else { /* JSON */
    pfp += sprintf(pfp, "\"ts\":\"%lld\",", evs[e].lastupd);
  }
  pfp = printonefloatsensor(t, pfp, "temp",      "%.2f", evs[e].temp);
  pfp = printonefloatsensor(t, pfp, "hum",       "%.1f", evs[e].hum);
  pfp = printonefloatsensor(t, pfp, "press",     "%.3f", evs[e].press);
  pfp = printonefloatsensor(t, pfp, "windspeed", "%.1f", evs[e].windspeed);
  pfp = printonefloatsensor(t, pfp, "winddir",   "%.1f", evs[e].winddirdeg);
  pfp = printonefloatsensor(t, pfp, "batvolt",   "%.2f", evs[e].batvolt);
  pfp = printonefloatsensor(t, pfp, "raingc",    "%.3f", evs[e].raingc);
  pfp = printonefloatsensor(t, pfp, "pm010",     "%.1f", evs[e].pm010);
  pfp = printonefloatsensor(t, pfp, "pm025",     "%.1f", evs[e].pm025);
  pfp = printonefloatsensor(t, pfp, "pm040",     "%.1f", evs[e].pm040);
  pfp = printonefloatsensor(t, pfp, "pm100",     "%.1f", evs[e].pm100);
  pfp = printonefloatsensor(t, pfp, "uvind",     "%.2f", evs[e].uvind);
  pfp = printonefloatsensor(t, pfp, "amblight",  "%.3f", evs[e].amblight);
  return pfp;
}

esp_err_t get_sensorshtml_handler(httpd_req_t * req)
{
  char myresponse[sizeof(senshtml_p1) + sizeof(senshtml_p2) + 2000];
  char * pfp; /* Pointer for (s)printf */
  strcpy(myresponse, senshtml_p1);
  pfp = myresponse + strlen(myresponse);
  pfp = printallsensors(0, pfp);
  strcpy(pfp, senshtml_p2);
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

esp_err_t get_mobilestate_handler(httpd_req_t * req)
{
  char myresponse[sizeof(mobstahtml_p1) + sizeof(mobstahtml_p2) + MOSTLEN + 500];
  char * pfp; /* Pointer for (s)printf */
  int e = activeevs;
  strcpy(myresponse, mobstahtml_p1);
  pfp = myresponse + strlen(myresponse);
  pfp += sprintf(pfp, "lastupdate TS: %lld (%lld seconds ago)<br>",
                      evs[e].lastupd, (time(NULL) - evs[e].lastupd));
  pfp += sprintf(pfp, "status:<br><pre>%s</pre>", evs[e].modemstatus);
  strcpy(pfp, mobstahtml_p2);
  /* The following two lines are the default und thus redundant. */
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=29");
  httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static httpd_uri_t uri_getmobilestate = {
  .uri      = "/mobilestate",
  .method   = HTTP_GET,
  .handler  = get_mobilestate_handler,
  .user_ctx = NULL
};

/* Unescapes a x-www-form-urlencoded string.
 * Modifies the string inplace! */
void unescapeuestring(char * s) {
  char * rp = s;
  char * wp = s;
  while (*rp != 0) {
    if (strncmp(rp, "&amp;", 5) == 0) {
      *wp = '&'; rp += 5; wp += 1;
    } else if (strncmp(rp, "%26", 3) == 0) {
      *wp = '&'; rp += 3; wp += 1;
    } else if (strncmp(rp, "%3A", 3) == 0) {
      *wp = ':'; rp += 3; wp += 1;
    } else if (strncmp(rp, "%2F", 3) == 0) {
      *wp = '/'; rp += 3; wp += 1;
    } else {
      *wp = *rp; wp++; rp++;
    }
  }
  *wp = 0;
}

#define POSTCONTMAXLEN 600
int parsepostandcheckauth(httpd_req_t * req, char * postcontent)
{
  char tmp1[100];
  if (req->content_len >= POSTCONTMAXLEN) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    const char myresponse[] = "Sorry, your request was too large.";
    httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    return 1;
  }
  int ret = httpd_req_recv(req, postcontent, req->content_len);
  if (ret < req->content_len) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    const char myresponse[] = "Your request was incompletely received.";
    httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    return 1;
  }
  postcontent[req->content_len] = 0;
  ESP_LOGI("webserver.c", "Received data: '%s'", postcontent);
  if (httpd_query_key_value(postcontent, "adminpw", tmp1, sizeof(tmp1)) != ESP_OK) {
    httpd_resp_set_status(req, "403 Forbidden");
    const char myresponse[] = "Permission denied - no adminpw submitted.";
    httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    return 1;
  }
  unescapeuestring(tmp1);
  if (strcmp(tmp1, MOBILEWS_WEBIFADMINPW) != 0) {
    ESP_LOGI("webserver.c", "Incorrect AdminPW - UE: '%s'", tmp1);
    httpd_resp_set_status(req, "403 Forbidden");
    const char myresponse[] = "Admin-Password incorrect.";
    httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    return 1;
  }
  return 0;
}

esp_err_t post_adminmenu(httpd_req_t * req) {
  char postcontent[POSTCONTMAXLEN];
  char myresponse[3000];
  char * pfp; /* Pointer for (s)printf */
  if (parsepostandcheckauth(req, postcontent) != 0) {
    return ESP_OK;
  }
  strcpy(myresponse, admmenhtml_p1);
  pfp = myresponse + strlen(myresponse);
  pfp += sprintf(pfp, "<input type=\"hidden\" name=\"adminpw\" value=\"%s\">", MOBILEWS_WEBIFADMINPW);
  strcpy(pfp, admmenhtml_p2);
  /* The following two lines are the default und thus redundant. */
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=29");
  httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static httpd_uri_t uri_postadminmenu = {
  .uri      = "/adminmenu",
  .method   = HTTP_POST,
  .handler  = post_adminmenu,
  .user_ctx = NULL
};

esp_err_t post_adminqueuecmd(httpd_req_t * req) {
  char postcontent[POSTCONTMAXLEN];
  char mode[20];
  if (parsepostandcheckauth(req, postcontent) != 0) {
    return ESP_OK;
  }
  if (httpd_query_key_value(postcontent, "mode", mode, sizeof(mode)) != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    const char myresponse[] = "No mode selected.";
    httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  if (strcmp(mode, "opsel") == 0) {
    char operator[20];
    int res = 0;
    if (httpd_query_key_value(postcontent, "operator", operator, sizeof(operator)) != ESP_OK) {
      httpd_resp_set_status(req, "400 Bad Request");
      const char myresponse[] = "No operator selected.";
      httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }
    if (strcmp(operator, "NULL") == 0) {
      res = mn_queuecommand("AT+COPS=0,0\r\n");
    } else {
      char permanent[20];
      char cmdtoq[40];
      if (httpd_query_key_value(postcontent, "permanent", permanent, sizeof(permanent)) != ESP_OK) {
        strcpy(permanent, "0");
      }
      sprintf(cmdtoq, "AT+COPS=%d,2,\"%s\"\r\n",
                      (strcmp(permanent, "1") == 0) ? 1 : 4, operator);
      res = mn_queuecommand(cmdtoq);
    }
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/html");
    if (res == 0) {
      const char myresponse[] = "A command has been queued.";
      httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    } else {
      const char myresponse[] = "Failed to queue command (buffer full).";
      httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
  } else {
    httpd_resp_set_status(req, "400 Bad Request");
    const char myresponse[] = "No valid mode selected.";
    httpd_resp_send(req, myresponse, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  return ESP_OK;
}

static httpd_uri_t uri_postadminqueuecmd = {
  .uri      = "/adminqueuecmd",
  .method   = HTTP_POST,
  .handler  = post_adminqueuecmd,
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
  httpd_register_uri_handler(server, &uri_getjson);
  httpd_register_uri_handler(server, &uri_getmobilestate);
  httpd_register_uri_handler(server, &uri_getsensorshtml);
  httpd_register_uri_handler(server, &uri_postadminmenu);
  httpd_register_uri_handler(server, &uri_postadminqueuecmd);
}

