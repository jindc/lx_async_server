#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include "lx_socket.h"
#include "lx_async_server.h"
#include "lxlog.h"
#include "lx_http.h"
#include "lx_epoll.h"
#include "lx_conn_ctx.h"
#include "lx_async_handler.h"

int init_lxasync_server(lx_bool_t is_nostub, const char * home,int thread_num)
{
    char buff[1024];
    int ret = 0;
    struct lxasync_server_ctx * ctx;
    ctx = (lxasync_server_ctx *)calloc(1,sizeof(lxasync_server_ctx));
    if( ctx == NULL)
    {
        perror("malloc in init error\n");
        return -1;
    }
    
    ctx->conn_timeout = 2000;//milli second
    ctx->epoll_timeout = 2000;
    ctx->epoll_maxevents = 100;

    ctx->is_nostub = is_nostub;
   
    if(home)
        g_home = (char *)home;
 
    if(pthread_mutex_init(&ctx->mutex,NULL) ){
        perror("pthread_mutex_init error");
        free(ctx);
        return -1;
    }

    newlxlog( (&ctx->log));
    ctx->asarg.newhour = 18;
    ctx->log.arg = &ctx->asarg;
    if(snprintf(buff,1024,"%s/%s",g_home,g_loghome) <=0){
        perror("snprintf log path error");
        goto err1;
    } 
    
    if(lxlog_init(&ctx->log,buff,"access.log", LX_LOG_DEBUG)) {
        printf("init log error,ret=%d",ret);
        ret = -1;goto err1;
    }
    ctx->log.flushnow = 1;
    ctx->log.tlockflag = 1;
    ctx->log.plockflag = 0;
    ctx->log.showpid = 1;
   // ctx->log.showtid = 0;

    g_lxasync_server_ctx = ctx;
    if ( thread_num <= 0 && (thread_num = sysconf(_SC_NPROCESSORS_ONLN) ) < 0){
            perror("get cpu core number error");
            ret = -1;goto err;
    }
    g_ctx->thread_num = thread_num;
    
    return 0;

err:
    ctx->log.cleanup(&g_ctx->log);     
err1:    
    pthread_mutex_destroy(&g_ctx->mutex);
    if(ctx != NULL)
        free(ctx);
    return ret;
}

int cleanup_lxasync_server()
{
    if( g_ctx != NULL)
    {
        g_ctx->log.cleanup(&g_ctx->log);     
        
        pthread_mutex_destroy(&g_ctx->mutex);
        
        free(g_ctx); 
        
        g_ctx = NULL; 
    }
    return 0;
}

static int do_service(void * arg);

int start_lxasync_server(int port)
{
    int i,ret,listen_fd ;
    pthread_t tid;
    if( (listen_fd = lx_listen(port)) < 0 ){
        g_ctx->log.logerror( &g_ctx->log, "lx_listen error[%d:%s]",ret, strerror(ret));
        return -1;
    } 
    
    for(i = 0; i < g_ctx->thread_num; ++i){
       if( ret = pthread_create(&tid,NULL,(void * (*)(void *))do_service,(void *)(long)listen_fd )){
            g_ctx->log.logerror( &g_ctx->log, "pthread_create error[%d:%s]",ret, strerror(ret));
            return -1;
       }

       if( ret = pthread_detach(tid)){
            g_ctx->log.logerror( &g_ctx->log, "pthread_detach error[%d:%s]",ret, strerror(ret));
            return -1;
       }

       g_ctx->log.loginfo(&g_ctx->log,"server %ld start",(long)tid);
    }

    g_ctx->log.loginfo(&g_ctx->log,"start server ok. the work thread number is %d",g_ctx->thread_num);

    while(1)
        sleep(5);
end:
    if(listen_fd >=0)
        close(listen_fd);
    
    return 0;    
}

static int do_service(void * arg)
{
    int ret = 0, ep_fd = -1,listen_fd = -1;
    struct epoll_event *events = NULL;
    lxasync_conn_ctx* listen_ctx = NULL;
    lx_rbtree_t timer;
    
    lx_rbtree_init(&timer,malloc,free);
    listen_fd = (int)(long)arg;

    if( (ep_fd = epoll_create(9)) < 0 ){
        g_ctx->log.logerror(&g_ctx->log,"epoll_create error");
        return -1;
    }
    
    if( (events = (struct epoll_event *)malloc( g_ctx->epoll_maxevents * sizeof(struct epoll_event) ) ) 
        == NULL){
        g_ctx->log.logerror(&g_ctx->log,"malloc epoll events error");
        ret = -1; goto end;
    }
     
    if( (listen_ctx = new_conn_ctx(ep_fd,listen_fd,listen_handle,&timer)) == NULL)
    {
        g_ctx->log.logerror(&g_ctx->log,"new_conn_ctx error");
        ret = -1;goto end;
    }
    listen_ctx->stage = STAGE_LISTENING;
    
    if( lx_set_epoll(ep_fd,listen_fd,listen_ctx,EPOLLIN|EPOLLET,LX_TRUE,LX_TRUE )){
        g_ctx->log.logerror(&g_ctx->log,"lx_set_epoll error" );
        ret = -1; goto end;
    }
   
    for(;;){
        ret = epoll_wait(ep_fd,events,g_ctx->epoll_maxevents,g_ctx->epoll_timeout);
        if(ret < 0){
            if(errno != EINTR){
                g_ctx->log.logerror(&g_ctx->log,"epoll_wait error");
                ret = -1;goto end;
            }else
                continue;
        }else if(ret > 0)
            handle_events(ep_fd,events,ret,&timer);
    }

    ret = 0;
end:
    lx_rbtree_free(&timer);

    if(ep_fd >=0)
        close(ep_fd);
    
    if(events)
        free(events);
    
    if(listen_ctx)
        if(cleanup_conn_ctx(listen_ctx)){
            g_ctx->log.logerror(&g_ctx->log,"cleanup conn ctx error");
    }
    return ret;    
}


