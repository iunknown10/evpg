#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <libpq-fe.h>
#include <event.h>
#include <evhttp.h>
#include "evpg.h"

struct evpg_cfg *
evpg_config_init(void)
{
    struct evpg_cfg *config;

    config = calloc(sizeof(struct evpg_cfg), 1);
    return config;
}

static struct evpg_db_list *
evpg_db_exists(struct evpg_db_node *node, struct evpg_db_list *list)
{
    struct evpg_db_list *s;

    s = list;

    while(s)
    {
	if (s->db == node)
	    break;

	s = s->next;
    }

    return s;
}

static void 
evpg_set_active(struct evpg_cfg *config, struct evpg_db_node *node)
{
    struct evpg_db_list *checker;
    struct evpg_db_list *prev, *next;

    /* first check to see if we are already active/deactive */
    if ((!(checker = evpg_db_exists(node, config->dbs.active))) &&
	    (!(checker = evpg_db_exists(node, config->dbs.ready))))
    {
	struct evpg_db_list *new_list;
	new_list = calloc(sizeof(struct evpg_db_list), 1);
	new_list->db = node;
	new_list->next = NULL;
	new_list->prev = NULL;
	config->dbs.active = new_list;
	return;
    }

    checker = evpg_db_exists(node, config->dbs.ready);

    prev = checker->prev;
    next = checker->next;


    /* remove it from the current list */
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    
    if (checker == config->dbs.ready)
	config->dbs.ready = next;

    /* add it to the active list */
    checker->prev = NULL;
    checker->next = NULL;

    checker->next = config->dbs.active;

    if(config->dbs.active)
	config->dbs.active->prev = checker;

    config->dbs.active = checker;
}

static void 
evpg_set_ready(struct evpg_cfg *config, struct evpg_db_node *node)
{
    struct evpg_db_list *checker;
    struct evpg_db_list *prev, *next;

    if ((!(checker = evpg_db_exists(node, config->dbs.active))) &&
            (!(checker = evpg_db_exists(node, config->dbs.ready))))
    { 
	struct evpg_db_list *new_list;
	new_list = calloc(sizeof(struct evpg_db_list), 1);
	new_list->db = node;
	new_list->next = NULL;
	new_list->prev = NULL;
	config->dbs.ready = new_list;
	return;
    }

    checker = evpg_db_exists(node, config->dbs.active);

    prev = checker->prev;
    next = checker->next;

    if (prev) prev->next = next;
    if (next) next->prev = prev;
    
    if (checker == config->dbs.active)  
	config->dbs.active = next;

    checker->next = NULL;
    checker->prev = NULL;

    checker->next = config->dbs.ready;

    if(config->dbs.ready)
	config->dbs.ready->prev = checker;

    config->dbs.ready = checker;
}

static void
evpg_query_finished(int sock, short which, void **data)
{
    struct evpg_db_node *dbnode;
    struct evpg_cfg *config;
    const char *querystr;
    void (*cb)(PGresult *, void *); 
    void *usrdata;
    struct event *event;

    config   = data[0];
    querystr = data[1];
    cb       = data[2];
    usrdata  = data[3];
    dbnode   = data[4];
    event    = data[5];

    PQconsumeInput(dbnode->dbconn);

    if (PQisBusy(dbnode->dbconn) == 0)
    {
	PGresult *result;
	result = PQgetResult(dbnode->dbconn);
	cb(result, usrdata);
	PQclear(result);
	free(event);
	free(data);
	evpg_set_ready(config, dbnode);
	return;
    }

    /* this query has not finished */
    event_set(event, sock, EV_WRITE, (void *)evpg_query_finished, data);
    event_add(event, 0);
}

static struct evpg_db_node *
evpg_snatch_connection(struct evpg_cfg *config)
{
    if (config->dbs.ready == NULL)
    {
	return NULL;
    }

    return config->dbs.ready->db;
}

static void 
evpg_make_evquery(int sock, short which, void **data)
{
    struct evpg_db_node *dbnode;
    struct evpg_cfg *config;
    const char *querystr;
    void (*cb)(PGresult *, void *);
    void *usrdata;
    struct event *event;

    config   = data[0];
    querystr = data[1];
    cb       = data[2];
    usrdata  = data[3];
    dbnode   = data[4];
    event    = data[5];

    if (!dbnode)
    {
       	if (!(dbnode = evpg_snatch_connection(config)))
	{
	    event_set(event, sock, EV_WRITE, (void *)evpg_make_evquery, data);
	    event_add(event, 0);
	    return;
	}
    }


    PQsendQuery(dbnode->dbconn, querystr);

    if (PQstatus(dbnode->dbconn) != CONNECTION_OK)
    {
	cb(NULL, usrdata);
	evpg_set_ready(config, dbnode);
	free(event);
	free(data);
    }

    data[4] = dbnode;

    evpg_set_active(config, dbnode);
    event_set(&dbnode->event, sock, EV_WRITE, (void *)evpg_query_finished, data);
    event_add(&dbnode->event, 0);
}

void evpg_make_query(struct evpg_cfg *config, 
       char *querystr, void (*cb)(PGresult *, void *), 
       void *usrdata)
{
    void **data;
    struct event *event;

    event = calloc(sizeof(struct event), 1);
    data = malloc(sizeof(void *) * 6);

    data[0] = config;
    data[1] = querystr;
    data[2] = cb;
    data[3] = usrdata;
    data[4] = NULL;
    data[5] = event;

    evpg_make_evquery(0, 0, data);
    //event_set(event, 0, EV_WRITE, (void *)evpg_make_evquery, data);
    //event_add(event, 0);
}

static void
evpg_connect_check(int sock, short which, void **usrdata)
{
    struct evpg_cfg *config;
    struct evpg_db_node *dbnode;

    config = usrdata[0];
    dbnode = usrdata[1];

    switch(PQconnectPoll(dbnode->dbconn))
    {
	case PGRES_POLLING_WRITING:
	    event_set(&dbnode->event, sock, EV_WRITE,
		    (void *)evpg_connect_check, usrdata);
	    event_add(&dbnode->event, 0);
	    break;
	case PGRES_POLLING_READING:
	    event_set(&dbnode->event, sock, EV_WRITE,
		    (void *)evpg_connect_check, usrdata);
	    event_add(&dbnode->event, 0);
	    break;
	case PGRES_POLLING_OK:
	    PQsetnonblocking(dbnode->dbconn, 1);
	    evpg_set_ready(config, dbnode);
	    free(usrdata);
	    break;
	default:
	    /* this is probably bad and should deal with it :) */
	    printf("ERROR %s\n", PQerrorMessage(dbnode->dbconn));
	    break;
    }
}

int
evpg_connect(struct evpg_cfg *config, const char *connstr)
{
    int pgsock;
    int status;
    struct evpg_db_node *dbnode;
    void **usrdata;

    if (!(dbnode = calloc(sizeof(struct evpg_db_node), 1)))
	return -1;

    dbnode->dbconn = PQconnectStart(connstr);
    
    pgsock = PQsocket(dbnode->dbconn);

    /* set this dbnode into an active state since it is not 
       ready to be used by the calling application */
    evpg_set_active(config, dbnode);

    /* we want to pass both our config, and our node */
    usrdata = malloc(sizeof(void *) * 2);
    usrdata[0] = config;
    usrdata[1] = dbnode;

    /* start the non-blocking connect event */
    event_set(&dbnode->event, pgsock, EV_WRITE, 
	    (void *)evpg_connect_check, usrdata);
    event_add(&dbnode->event, 0);
}
