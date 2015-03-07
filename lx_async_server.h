#ifndef LX_ASYNC_SERVER_H
#define LX_ASYNC_SERVER_H
#include <pthread.h>
#include <sys/epoll.h>
#include "lx_socket.h"
#include "lxlog.h"
#include "lx_types.h"
#include "lx_http.h"
#include "lx_rbtree.h"

typedef struct lxasync_server_ctx lxasync_server_ctx;
struct lxasync_server_ctx 
{
    long conn_timeout;

    int epoll_timeout;
    int epoll_maxevents;

    lx_bool_t is_nostub;
    
    int thread_num;
    pthread_mutex_t mutex;
    
    struct lxlog log;
    struct lxlog_dailyas asarg;

} *g_lxasync_server_ctx;

#define g_ctx (g_lxasync_server_ctx)

typedef struct lxasync_thread_ctx lxasync_thread_ctx;
struct lxasync_thread_ctx
{
    lx_rbtree_t timer;    
};


typedef enum lxasync_handle_stat lxasync_handle_stat;
enum lxasync_handle_stat
{
    HANDLE_DONE = 0,
    HANDLE_NEED_MORE,
    HANDLE_ERR 
};

static char * g_home = "home";
static char * g_whome = "webhome";
static char * g_loghome = "logs";

int init_lxasync_server(lx_bool_t is_nostub, const char * home,int thread_num);

int start_lxasync_server(int port);

int cleanup_lxasync_server();

#endif
