gcc -W -Werror -g -I.  -I../lxlib -I../lx_http -I../lxlog -I../lx_rbtree  -l pthread -o lxasync ../lxlib/*.c ../lxlog/*.c ../lx_http/*.c *.c ../lx_rbtree/lx_rbtree.c
