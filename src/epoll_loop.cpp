#include "../include/epoll_loop.hpp"
#include "../include/match.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <cstdio>

int g_epoll_fd = -1;

bool init_epoll_loop() {
    g_epoll_fd = epoll_create1(0);
    if (g_epoll_fd < 0) {
        perror("epoll_create1");
        return false;
    }
    return true;
}

void register_fd_with_epoll(int fd) {
    if (g_epoll_fd < 0) return;
    struct epoll_event ev{};
    ev.events = EPOLLIN; // start watching for reads
    ev.data.fd = fd;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

void enable_epoll_write(int fd) {
    if (g_epoll_fd < 0) return;
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = fd;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

void disable_epoll_write(int fd) {
    if (g_epoll_fd < 0) return;
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

void run_epoll_loop() {
    if (g_epoll_fd < 0) return;
    const int MAX_EVENTS = 64;
    std::vector<struct epoll_event> events(MAX_EVENTS);

    using namespace pb;

    while (true) {
        int n = epoll_wait(g_epoll_fd, events.data(), MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        auto& ctx = get_match_context();

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (ev & EPOLLOUT) {
                std::lock_guard<std::mutex> lock(ctx.wsClientsMutex);
                for (auto it = ctx.wsClients.begin(); it != ctx.wsClients.end(); ++it) {
                    if (it->fd == fd) {
                        // attempt to flush outBuf
                        while (!it->outBuf.empty()) {
                            ssize_t s = send(fd, it->outBuf.data(), it->outBuf.size(), 0);
                            if (s < 0) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    break;
                                } else {
                                    // error, close and remove client
                                    close(fd);
                                    it = ctx.wsClients.erase(it);
                                    goto next_event;
                                }
                            }
                            it->outBuf.erase(0, (size_t)s);
                        }
                        if (it->outBuf.empty()) {
                            disable_epoll_write(fd);
                        }
                        break;
                    }
                }
            }
        next_event: ;
        }
    }
}
