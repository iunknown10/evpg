struct evpg_db_node {
    struct event event;
    PGconn *dbconn;
};

struct evpg_db_list {
    struct evpg_db_node *db;
    struct evpg_db_list *next;
    struct evpg_db_list *prev;
};

struct evpg_db {
    struct evpg_db_list *active;
    struct evpg_db_list *ready;
};

struct evpg_cfg {
    struct evpg_db dbs;
    struct event event;
    int debug;
};

struct evpg_cfg *evpg_config_init(void);
void evpg_make_query(struct evpg_cfg *config, char *querystr, void (*cb)(PGresult *, void *), void *usrdata);
int evpg_connect(struct evpg_cfg *config, const char *connstr);
