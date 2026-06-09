#pragma once
#include <sys/epoll.h>

bool init_epoll_loop();
void register_fd_with_epoll(int fd);
void enable_epoll_write(int fd);
void disable_epoll_write(int fd);
void run_epoll_loop();

extern int g_epoll_fd;
