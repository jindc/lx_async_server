#ifndef LX_CONN_CTX_H
#define LX_CONN_CTX_H
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <stdint.h>
#include "lx_socket.h"
#include "lx_async_server.h"
#include "lxlog.h"
#include "lx_http.h"
#include "lx_epoll.h"
#include "lx_rbtree.h"

#define MAX_HEADER_LEN (2048)
#define MAX_PATH_LEN (1024)

typedef struct lxasync_conn_ctx lxasync_conn_ctx; 
struct lxasync_conn_ctx
{
    int ep_fd;
    int fd;
    unsigned int events;
    
    struct sockaddr_in peer_addr;
    struct timeval accept_time;
    uint64_t timeout;//timeout time for timer
    lx_rbtree_t * timer;

    h_stage_t stage;
    int (*handle)(lxasync_conn_ctx * arg );
   
    h_parser_ctx req_ctx;
    h_parser_ctx resp_ctx;
    lx_buffer data_buff;
    char path[MAX_PATH_LEN];
    FILE * fh;
    int contlen;
    int inoutlen;

    void * arg;
    int (*cleanup)(lxasync_conn_ctx * arg );
};

static lxasync_conn_ctx * new_conn_ctx(int ep_fd,int fd,int (*handle)(lxasync_conn_ctx * ctx),lx_rbtree_t *timer)
{
    char * buff;
    int ret = 0;
    lxasync_conn_ctx *pctx = NULL;

    if( (pctx = (lxasync_conn_ctx *)calloc(1, sizeof(lxasync_conn_ctx))) == NULL)
        return NULL;
    
    http_set_uri_sep(&pctx->req_ctx,'?','&','=');
    http_set_memory_msuit(&pctx->req_ctx,malloc,free,http_extend);
    if(http_ctx_init(&pctx->req_ctx,T_REQ,256)){
        ret =-1;goto err;
    }
    
    http_set_uri_sep(&pctx->resp_ctx,'?','&','=');
    http_set_memory_msuit(&pctx->resp_ctx,malloc,free,http_extend);
    if(http_ctx_init(&pctx->resp_ctx,T_RESP,2 )){
        ret = -1; goto err1;    
    }
    
    if( (buff = (char *)malloc(MAX_HEADER_LEN)) == NULL){
        ret = -1;goto err2;    
    }
    lx_buffer_init(&pctx->data_buff,buff,0,0,MAX_HEADER_LEN);

    pctx->ep_fd = ep_fd;
    pctx->fd = fd;
    gettimeofday(&pctx->accept_time,NULL);

    pctx->stage = STAGE_START;
    pctx->handle = handle;
    pctx->timer = timer;

    return pctx;

err2:
    http_ctx_cleanup(&pctx->resp_ctx);
err1:
   http_ctx_cleanup(&pctx->req_ctx ); 
err:
    free(pctx);
    return NULL;

}

static int cleanup_conn_ctx(lxasync_conn_ctx * pctx)
{
    int ret = 0;

//    if(pctx->fd >= 0)
//      close(pctx->fd);
    pctx->fd = -1;
    
    http_ctx_cleanup(&pctx->req_ctx);
    http_ctx_cleanup(&pctx->resp_ctx);
    
    free(pctx->data_buff.base);

    if(pctx->cleanup){
        if( pctx->cleanup(pctx)){
            ret = -1;
        } 
    }
    
    free(pctx);

    return ret = 0;
}

#endif
