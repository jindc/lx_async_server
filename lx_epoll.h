#ifndef LX_EPOLL_H
#define LX_EPOLL_H

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include "lx_types.h"

int lx_set_epoll(int ep_fd, int fd, void * data, int events,lx_bool_t is_nblk,lx_bool_t is_add);

char * lx_get_events(unsigned int events,char * buff, size_t len );

#endif
