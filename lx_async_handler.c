#include <stdint.h>
#include <assert.h>
#include "lx_async_server.h"
#include "lx_async_handler.h"
#include "lx_fileio.h"
#include "lxtime.h"

int req_head_handle(lxasync_conn_ctx * );
int req_body_handle(lxasync_conn_ctx * );
int resp_head_handle(lxasync_conn_ctx * );
int resp_body_handle(lxasync_conn_ctx *);

#define AGAIN_FLAG (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
int remove_conn(lxasync_conn_ctx *pctx)
{
    if(pctx->fd >=0 && epoll_ctl(pctx->ep_fd,EPOLL_CTL_DEL,pctx->fd,NULL)){
        g_ctx->log.logerror(&g_ctx->log,"delete fd:%d from epll error",pctx->fd);    
    }

    if(pctx->fd >=0){
        close(pctx->fd);
        pctx->fd = -1;
    }

    cleanup_conn_ctx(pctx);
    return 0;    
}

int handle_events(int ep_fd,struct epoll_event * events, int nevent,lx_rbtree_t * timer)
{
    int i = 0, ret = 0;
    uint64_t key_temp;
    char ebuff[1024] ,*pebuff;
    lxasync_conn_ctx * cctx;
    lx_rbtree_node * n;

    //if (nevent) printf("handle events ,nevent :%d\n",nevent);
    for(; i < nevent;++i){
        cctx = (lxasync_conn_ctx*)events[i].data.ptr;
        cctx->events = events[i].events;

        pebuff = lx_get_events(cctx->events,ebuff,1024 ); 
        //printf("events flags:%s\n",pebuff);
        
        ret = cctx->handle( cctx);
    }

    key_temp = (uint64_t)get_micros();
    while(1){
        n = lx_rbtree_min( timer,timer->root);
        if(n != &timer->nil ){
            
            if(n->key == 0){
                printf("you\n");    
            }
            
            if (n->key < key_temp){
                uint64_t key = n->key;

                g_ctx->log.loginfo(&g_ctx->log,"timer:connection timeout");
                //printf("delete key in timer check:%lu\n",(unsigned long)n->key);
                 
                remove_conn(n->data);
                if(lx_rbtree_delete(timer,n->key ) != 0){
                    g_ctx->log.logfatal(&g_ctx->log,"timer: cannot find key %lu for delete",(unsigned long) key);
                }

                //assert( !lx_rbtree_find(timer,key));

            }else{
                break;    
            }
        }else{
            break;    
        }            
    }

    return 0;
}

int listen_handle(lxasync_conn_ctx *arg )
{
    int ret = 0,addrlen = 0,fd = -1;
    struct sockaddr_in addr;
    lxasync_conn_ctx * cctx = NULL;
    uint64_t l_temp;

    //printf("listen handle call\n");
    if(arg->events & EPOLLERR ){
        g_ctx->log.logerror(&g_ctx->log,"listen fd from epoll error");
        return 0;
    }

    addrlen = sizeof(struct sockaddr_in);
    if(pthread_mutex_trylock(&g_ctx->mutex) == 0){
        fd = accept(arg->fd,(struct sockaddr *)&addr,&addrlen);
        if(pthread_mutex_unlock(&g_ctx->mutex)){
             g_ctx->log.logerror(&g_ctx->log,"%s:pthread_mutex_unlock error",__FUNCTION__);
        }
    }else{
        return 0;
    }
    
    if(fd < 0){ 
        if(errno == EAGAIN|| errno == EWOULDBLOCK) {
            //g_ctx->log.logdebug(&g_ctx->log,"accept return again or block,%d:%s",errno,strerror(errno));
            return 0;
        }else{
            g_ctx->log.logerror(&g_ctx->log,"accept error");
            return 0;
        }
    }
    
    if((cctx = new_conn_ctx(arg->ep_fd,fd,conn_handle,arg->timer )) == NULL){
        g_ctx->log.logerror(&g_ctx->log,"%s:new_conn_ctx error",__FUNCTION__);
        ret = -1; goto err; 
    }
    cctx->peer_addr = addr;
    
    if(lx_set_epoll(arg->ep_fd,cctx->fd,cctx,EPOLLIN|EPOLLET,LX_TRUE,LX_TRUE)){
        g_ctx->log.logerror(&g_ctx->log,"lx_set_epoll error");
        ret = -1;goto err;
    }
    
    l_temp = get_micros() + g_ctx->conn_timeout*1000;
    while( lx_rbtree_insert(arg->timer,(uint64_t)l_temp,cctx ) != 0){   
        l_temp += 1;
    }
    cctx->timeout = l_temp;
    //printf("insert key:%lu\n",(unsigned long)l_temp);

    ret = 0;
    //printf("listen handle call end\n");
    return ret;
err:
    if(cctx)
        cleanup_conn_ctx(cctx);

    if(fd >= 0){
        close(fd);
    }
    return ret;    
}

int record_end_log(lxasync_conn_ctx * cctx)
{   
    char buff[1024];
    int ret;

    if( !inet_ntop(AF_INET, (void *)&cctx->peer_addr.sin_addr, buff,16) ){
        g_ctx->log.logerror(&g_ctx->log,"inet_ntop error");
        ret = -1;goto end;
    }
    
    if(getwidetime(time(NULL),buff +16,32) <=0){
        g_ctx->log.logerror(&g_ctx->log,"get_wide_time error");
        ret = -1;goto end;
    }

    g_ctx->log.loginfo(&g_ctx->log,"uri:%s,addr:%s:%d,start:%s,duration:%ld"
        ,http_get_uri(&cctx->req_ctx.info),buff,(int)ntohs(cctx->peer_addr.sin_port),
        buff+16,get_inval_micros(&cctx->accept_time ,NULL)); 
    
    ret = 0;
end:
    return ret;
}

int conn_handle(lxasync_conn_ctx * cctx)
{
    int ret;
    //printf("conn_handle call\n");
 
 #define lx_handle_macro( handle ) \
        if( (ret = (handle)(cctx) ) == HANDLE_NEED_MORE )\
            return 0;\
        else if( ret == HANDLE_ERR )\
            goto done;

    if(cctx->events & EPOLLERR || cctx->events &EPOLLHUP){
        g_ctx->log.logerror(&g_ctx->log,"%s:connection error occur",__FUNCTION__);
        goto done;
     }

    switch(cctx->stage){
        
    case STAGE_START:
        cctx->stage = STAGE_REQ_HEAD;
    
    case STAGE_REQ_HEAD:
        lx_handle_macro(req_head_handle)
        cctx->stage = STAGE_REQ_BODY;
        cctx->contlen = cctx->inoutlen = 0;
/*
        if(http_print_http(&cctx->req_ctx)){
            g_ctx->log.logerror(&g_ctx->log,"print_pare_info error");
        }
*/
    case STAGE_REQ_BODY:
        lx_handle_macro(req_body_handle)
        cctx->stage = STAGE_RESP_HEAD;
        cctx->data_buff.offset = cctx->data_buff.len = 0;
        if(lx_set_epoll(cctx->ep_fd,cctx->fd,cctx,EPOLLOUT|EPOLLET,LX_TRUE,LX_FALSE )){
            g_ctx->log.logerror(&g_ctx->log,"%s:lx_set_epoll error",__FUNCTION__);
            goto done;
        }
        return 0;

    case STAGE_RESP_HEAD:
        lx_handle_macro(resp_head_handle)
        cctx->stage = STAGE_RESP_BODY;
        cctx->fh = NULL;

    case STAGE_RESP_BODY:
        lx_handle_macro(resp_body_handle)
        cctx->stage = STAGE_DONE;
        record_end_log(cctx);      
    default:
        goto done;
    }
#undef lx_handle_macro

done:

    ret = lx_rbtree_delete(cctx->timer,(uint64_t)cctx->timeout);
    //printf("delete in done, key:%lu,ret:%d\n",(unsigned long)cctx->timeout,ret);
    remove_conn(cctx);
    return 0;
}


int req_head_handle(lxasync_conn_ctx *pctx )
{
    int ret = 0,read_num = 0,to_read_num = 0;
    char * buff;
    while(1)
	{
        buff = lx_buffer_lenp(&pctx->req_ctx.orig_buff);
        to_read_num = lx_buffer_freenum(&pctx->req_ctx.orig_buff);

		read_num = recv(pctx->fd,buff,to_read_num,0);
		if( read_num < 0){
            if(AGAIN_FLAG){
                ret = HANDLE_NEED_MORE;
                goto end;
             }else{
			    g_ctx->log.logerror(&g_ctx->log,"%s:recv error",__FUNCTION__);
                ret = HANDLE_ERR;goto end;
             }
        }else if( read_num == 0){
			g_ctx->log.logerror(&g_ctx->log,"cannot get enough head info");
            ret = HANDLE_ERR;goto end;
		}else{
		    pctx->req_ctx.orig_buff.len += read_num;
		    ret = http_parse(&pctx->req_ctx);
            
            if( ret == HEC_OK){
                //copy to data_buff
                int tocopy = lx_buffer_unscannum( &pctx->req_ctx.orig_buff);
                if(tocopy >= pctx->data_buff.maxlen){
                    g_ctx->log.logerror(&g_ctx->log,"req body too big to copy to data_buff");
                    ret = HANDLE_ERR;
                    goto end;
                }
                memcpy(pctx->data_buff.base,lx_buffer_offsetp(&pctx->req_ctx.orig_buff),tocopy);
                pctx->data_buff.len = tocopy;

                ret = HANDLE_DONE;
                goto end;
            }else if( ret == HEC_NEED_MORE)
                continue;
            else{
                g_ctx->log.logerror(&g_ctx->log,"parser error[%d]",ret);
                ret = HANDLE_ERR;goto end;
            }
	    }
    }

end:
    return ret;
}

int req_body_handle(lxasync_conn_ctx *pctx )
{
    int ret,nleft,to_read_num,read_num ;
    lx_buffer *data;
    
    data = &pctx->data_buff;
	pctx->contlen =http_get_contlen(&pctx->req_ctx.info);

    if(pctx->contlen <= 0 )
        return HANDLE_DONE;
    
    if(pctx->inoutlen == 0){
        pctx->inoutlen = pctx->contlen > data->len? data->len:pctx->contlen ;

        printf("req_body:\n");
        printf("%.*s",pctx->inoutlen,data->base);   
    }

    while( pctx->inoutlen < pctx->contlen ){

        nleft = pctx->contlen - pctx->inoutlen;
        to_read_num = nleft < data->maxlen? nleft : data->maxlen;
        read_num = recv(pctx->fd,data->base, to_read_num,0  );
        
        if(read_num < 0){
            if(AGAIN_FLAG ){
                ret = HANDLE_NEED_MORE;
                goto end;
            }else{
                g_ctx->log.logerror(&g_ctx->log,"%s:recv err",__FUNCTION__);
                ret = HANDLE_ERR;
                goto end;
            }
        }else if(read_num == 0){
            g_ctx->log.logerror(&g_ctx->log,"%s:can not get enough req body",__FUNCTION__);
            ret = HANDLE_ERR;
            goto end;
        }else{
            printf("%.*s",read_num,data->base );
            pctx->inoutlen += read_num;    
        }
    }

    ret = HANDLE_DONE;

end:
    return ret;
}

int resp_head_handle(lxasync_conn_ctx *cctx )
{
    int ret, send_num,to_send_num, rcode;
    char date[64] ,* uri,*rstr;
    h_parser_ctx * resp_ctx;

    char * headers[] = {  
        "Content-Type" ,"text/html",
        "Connection"   ,"Keep-Alive",
        "Server"       ,"lanxin/spl 1.0",
        NULL,NULL
    };

    resp_ctx = &cctx->resp_ctx;
    if(cctx->data_buff.len == 0){    
        if( !(ret = get_browser_time( time(NULL),date,64 ) ) ){
            g_ctx->log.logerror(&g_ctx->log,"%s:snprintf date error",__FUNCTION__);
            ret = HANDLE_ERR;goto err;
        }

        uri = http_get_uri(&cctx->req_ctx.info);
        if( uri == NULL || strcmp(uri, "/") == 0)
            uri = "/index.html";
        if( (ret = snprintf(cctx->path,MAX_PATH_LEN,"%s/%s%s",g_home,g_whome,uri)) <= 0 ){
            g_ctx->log.logerror(&g_ctx->log,"%s:snprintf path error",__FUNCTION__);
            ret = HANDLE_ERR;goto err;
        }
        
        if( (cctx->contlen = lx_get_fsize(cctx->path) )== -1){
            rcode = 404;
            rstr = "File Not Found";
           
            g_ctx->log.logerror(&g_ctx->log,"uri invalid ,404,uri:%s", uri);

            if( (ret = snprintf(cctx->path,MAX_PATH_LEN,"%s/%s%s",g_home,g_whome,"/404.html")) <= 0 ){
                g_ctx->log.logerror(&g_ctx->log,"%s:snprintf path error,ret = %d",__FUNCTION__,ret);
                ret = HANDLE_ERR;goto err;
            }
            if( ( cctx->contlen = lx_get_fsize(cctx->path))== -1){
                g_ctx->log.logerror(&g_ctx->log,"open 404 file error:%s",cctx->path);
                ret = HANDLE_ERR;goto err;
            }
        }else{
            rcode = RESP_OK;
            rstr = NULL;
        }
        
        if( http_set_prot(resp_ctx,P_HTTP_1_1))
        {
            g_ctx->log.logerror(&g_ctx->log,"set prot error");
            ret = HANDLE_ERR;goto err;
        }

        if( http_set_rcode(resp_ctx,rcode,rstr))
        {
            g_ctx->log.logerror(&g_ctx->log,"set resp code error");
            ret = HANDLE_ERR;goto err;
        }

        if(http_set_headers(resp_ctx,headers,cctx->contlen )){
            g_ctx->log.logerror(&g_ctx->log,"set headers error");
            ret = HANDLE_ERR; goto err;
        }
        
        if(http_set_header(resp_ctx,"Date",date )){
            g_ctx->log.logerror(&g_ctx->log,"set headers error");
            ret = HANDLE_ERR; goto err;
        }

        if((cctx->data_buff.len = http_seri_head(&resp_ctx->info,T_RESP,cctx->data_buff.base,cctx->data_buff.maxlen)) <= 0){
            g_ctx->log.logerror(&g_ctx->log,"http_ctx_serihead error");
            ret = HANDLE_ERR;goto err;
        }
   }
 
    while( lx_buffer_unscannum(&cctx->data_buff) > 0  ){
        to_send_num = lx_buffer_unscannum(&cctx->data_buff);
        send_num = send(cctx->fd,lx_buffer_offsetp(&cctx->data_buff),to_send_num,0);
        if(send_num < 0){
            if( AGAIN_FLAG ){
                ret = HANDLE_NEED_MORE;
                goto err;
            }else{
                g_ctx->log.logerror(&g_ctx->log,"%s:send error", __FUNCTION__);
                ret = HANDLE_ERR;goto err;    
            }    
        }else{
            cctx->data_buff.offset += send_num;
            continue;    
        }
    }

    ret = HANDLE_DONE;
err:
    return ret;
}

int fill_send_buff(lxasync_conn_ctx * pctx)
{   int read_num,to_read_num;

    to_read_num = pctx->contlen - pctx->inoutlen > pctx->data_buff.maxlen ? pctx->data_buff.maxlen:pctx->contlen - pctx->inoutlen;
    read_num =freadn(pctx->fh,pctx->data_buff.base,to_read_num);
    
    if(read_num <= 0){
        g_ctx->log.logerror(&g_ctx->log, "%s:read response file %s error",__FUNCTION__,pctx->path);
        return -1;
    }
    
    pctx->data_buff.len = read_num;
    pctx->data_buff.offset = 0;

    return 0;
}

int resp_body_handle(lxasync_conn_ctx *pctx )
{
    int ret = HANDLE_DONE;
    if(pctx->fh == NULL){
        if( (pctx->fh = fopen(pctx->path,"rb" )) ==NULL){
            g_ctx->log.logerror(&g_ctx->log, "%s:open file %s error",__FUNCTION__,pctx->path);
            ret = HANDLE_ERR;goto end;
        }
        pctx->data_buff.offset = pctx->data_buff.len = 0;
        pctx->inoutlen = 0;

        if( fill_send_buff(pctx)){
            g_ctx->log.logerror(&g_ctx->log, "%s:fill_send_buff error %s ",__FUNCTION__,pctx->path);
            ret = HANDLE_ERR;goto end;
        }
        pctx->inoutlen += pctx->data_buff.len;
    }
    
    while(1){
        int send_num,to_send_num;

        to_send_num = lx_buffer_unscannum(&pctx->data_buff);
        send_num = send(pctx->fd,lx_buffer_offsetp(&pctx->data_buff ), to_send_num,0);
        if( send_num < 0){
            if(AGAIN_FLAG){
                ret = HANDLE_NEED_MORE;goto end;    
           }else{
                g_ctx->log.logerror(&g_ctx->log, "%s:send response body  error",__FUNCTION__);
                ret = HANDLE_ERR;goto end;
            }    
        }else if(send_num == 0){
             g_ctx->log.logerror(&g_ctx->log, "%s:send response body  error,send_num = 0",__FUNCTION__);
             ret = HANDLE_ERR;goto end;
        }else{
            pctx->data_buff.offset += send_num;
            
            if( pctx->data_buff.offset == pctx->data_buff.len){
                if( pctx->inoutlen == pctx->contlen){
                    ret = HANDLE_DONE; goto end;
                 }else{
                    if( fill_send_buff(pctx)){
                        g_ctx->log.logerror(&g_ctx->log, "%s:fill_send_buff error %s ",__FUNCTION__,pctx->path);
                        ret = HANDLE_ERR;goto end;
                    }
                    pctx->inoutlen += pctx->data_buff.len;
                    continue;
                }   
            }else{
                continue;    
            }
        }
    }//end send loop
    
    ret = HANDLE_DONE;
    
end:
    if(ret == HANDLE_ERR || ret == HANDLE_DONE){
        if(pctx->fh != NULL){
            fclose(pctx->fh);
            pctx->fh = NULL;
        }
    }
    return ret;
}
