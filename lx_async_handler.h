#ifndef LX_ASYNC_HANDLER_H
#define LX_ASYNC_HANDLER_H
#include "lx_conn_ctx.h"

int handle_events(int ep_fd,struct epoll_event * events, int nevent,lx_rbtree_t * timer);
int listen_handle(lxasync_conn_ctx * );
int conn_handle(lxasync_conn_ctx * );

#endif
