# lx_async_server
一个基于epoll的linux c web服务器

lx_async_server使用epoll实现异步web服务，同时可以指定线程的数量，每一个线程独立工作以充分利用硬件的cpu及内存等资源。默认情况下，服务器会根据cpu的个数起相应的线程数。定时器使用红黑树实现。整个架构参考nginx。

服务器经valgrind 内存测试及压力测试.
要被访问的网页放到 home/webhome 目录下

编译：
项目会用到其他模块

lx_http https://github.com/jindc/lx_rbtree.git
lx_http https://github.com/jindc/lx_http.git
lxlib https://github.com/jindc/lxlib.git
lxlog https://github.com/jindc/lxlog.git

./build.sh
./lxasync -h

usage:lxmt [-h] [--port] [--home] [--thread_num] [--daemon]

--thread_num 启动的独立线程数，默认与cpu数相同

作者：德才
email: jindc@163.com
