#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <libpq-fe.h>
#include <event.h>
#include <evhttp.h>
#include "evpg.h"

struct event event;
struct evpg_cfg *pgconfig;
char *request = NULL;

void 
webserver_db_fn(PGresult *res, 
	struct evhttp_request *usrdata)
{
    struct evbuffer *buf;
    buf = evbuffer_new();
    printf("query finished!\n");
    evbuffer_add_printf(buf, "TEST %s\n", usrdata->uri);
    evhttp_send_reply(usrdata, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}


void
webserver(struct evhttp_request *req, struct evpg_cfg *d)
{
    struct evbuffer *buf;

    evpg_make_query(d, request, (void *)webserver_db_fn, req);
    return;
}

void 
webserver_init(void)
{
    struct evhttp *httpd;
    httpd = evhttp_start("0.0.0.0", 9090);
    evhttp_set_gencb(httpd, (void *)webserver, pgconfig);
}

int main(int argc, char **argv)
{
    char *db;
    if (argc < 3)
    {
	printf("Usage: %s <connection string> <query>\n", argv[0]);
	exit(1);
    }

    db = argv[1];
    request = argv[2];

    pgconfig = evpg_config_init();
    event_init();
    evpg_connect(pgconfig, db);
    evpg_connect(pgconfig, db);
    evpg_connect(pgconfig, db);
    webserver_init();
    event_loop(0);
}



