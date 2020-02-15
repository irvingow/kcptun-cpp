//
// Created by lwj on 2020/2/3.
//

#include <sys/time.h>
#include <sys/epoll.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "kcptunnel_common.h"
#include <unistd.h>
#include <fcntl.h>

namespace kcptunnel {

int32_t AddEvent2Epoll(const int32_t &epoll_fd, const int32_t &fd, const uint32_t &events) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    auto ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    if (ret != 0) {
        LOG(INFO) << "add fd:" << fd << " to epoll_fd:" << epoll_fd << " failed, error:" << strerror(errno);
        return -1;
    }
    return 0;
}

int set_non_blocking(const int &fd) {
    int opts = -1;
    opts = fcntl(fd, F_GETFL);

    if (opts < 0) {
        LOG(ERROR) << "get socket status failed, fd:" << fd
                   << " error:" << strerror(errno);
        return -1;
    }
    if (opts & O_NONBLOCK)
        return 0;
    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0) {
        LOG(ERROR) << "set socket non_blocking failed, fd:" << fd
                   << " error:" << strerror(errno);
        return -1;
    }
}

int new_listen_socket(const std::string &ip, const size_t &port, int &fd, SocketType socketType) {
    if (socketType == TCP) {
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == -1) {
            LOG(ERROR) << "create new socket failed" << strerror(errno);
            return -1;
        }
        int reuse = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            LOG(ERROR) << "failed to call setsockopt error:" << strerror(errno);
            close(fd);
            return -2;
        }
    } else if (socketType == UDP) {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == -1) {
            LOG(ERROR) << "create new socket failed" << strerror(errno);
            return -1;
        }
    } else {
        LOG(ERROR) << "wrong socketType(currently support UDP and TCP";
        return -1;
    }
    struct sockaddr_in local_listen_addr = {0};
    local_listen_addr.sin_family = AF_INET;
    local_listen_addr.sin_port = htons(port);
    if (inet_pton(local_listen_addr.sin_family, ip.c_str(), &local_listen_addr.sin_addr) < 0) {
        LOG(ERROR) << "failed to call inet_pton error:" << strerror(errno);
        return -3;
    }
    socklen_t slen = sizeof(local_listen_addr);
    if (bind(fd, (struct sockaddr *) &local_listen_addr, slen) == -1) {
        LOG(ERROR) << "socket bind error port:" << port
                   << " error:" << strerror(errno);
        close(fd);
        return -4;
    }
    if (socketType == TCP) {
        if (listen(fd, 5) < 0) {
            LOG(ERROR) << "failed to call listen error:" << strerror(errno);
            close(fd);
            return -5;
        }
    }
    LOG(INFO) << "local socket listen_fd:" << fd;
    return 0;
}

int new_connected_socket(const std::string &remote_ip,
                         const size_t &remote_port, int &fd, SocketType socketType) {
    struct sockaddr_in remote_addr_in = {0};
    socklen_t slen = sizeof(remote_addr_in);
    remote_addr_in.sin_family = AF_INET;
    remote_addr_in.sin_port = htons(remote_port);
    if (inet_pton(remote_addr_in.sin_family, remote_ip.c_str(), &remote_addr_in.sin_addr) < 0) {
        LOG(ERROR) << "failed to call inet_pton error:" << strerror(errno);
        return -1;
    }
    if (socketType == TCP) {
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            LOG(ERROR) << "create new socket failed" << strerror(errno);
            return -1;
        }
    } else if (socketType == UDP) {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) {
            LOG(ERROR) << "create new socket failed" << strerror(errno);
            return -1;
        }
    }
    ///blocking connect, if network is bad, may block for a pretty long time
    int ret = connect(fd, (struct sockaddr *) &remote_addr_in, slen);
    if (ret < 0) {
        LOG(ERROR) << "failed to establish connection to remote, error:"
                   << strerror(errno);
        close(fd);
        return -1;
    }
    LOG(INFO) << "create new remote connection tcp_fd:" << fd;
    return 0;
}

int32_t kcptunnel_init(const std::string &remote_ip,
                       const int32_t &remote_port,
                       const std::string &local_listen_ip,
                       const int32_t &local_listen_port,
                       int32_t &epoll_fd,
                       int32_t &local_listen_fd,
                       int32_t &remote_connected_fd,
                       int32_t &timerfd) {
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG(ERROR) << "create epoll failed error:" << strerror(errno);
        return -1;
    }
    ///创建本地监听的local_listen_fd,同时将其加入epoll监听池中
    auto ret = new_listen_socket(local_listen_ip, local_listen_port, local_listen_fd, TCP);
    if (ret < 0) {
        LOG(ERROR) << "failed to new_listen_socket error:" << strerror(errno);
        close(epoll_fd);
        return -1;
    }
    ret = AddEvent2Epoll(epoll_fd, local_listen_fd, EPOLLIN);
    if (ret != 0) {
        close(local_listen_fd);
        close(epoll_fd);
        LOG(INFO) << "add local_udp_listen_fd to epoll failed, error:" << strerror(errno);
        return -1;
    }
}

void write_u32(char *p, uint32_t l) {
    *(unsigned char *) (p + 3) = (unsigned char) ((l >> 0) & 0xff);
    *(unsigned char *) (p + 2) = (unsigned char) ((l >> 8) & 0xff);
    *(unsigned char *) (p + 1) = (unsigned char) ((l >> 16) & 0xff);
    *(unsigned char *) (p + 0) = (unsigned char) ((l >> 24) & 0xff);
}

uint32_t read_u32(const char *p) {
    uint32_t res;
    res = *(const unsigned char *) (p + 0);
    res = *(const unsigned char *) (p + 1) + (res << 8);
    res = *(const unsigned char *) (p + 2) + (res << 8);
    res = *(const unsigned char *) (p + 3) + (res << 8);
    return res;
}

void write_u16(char *p, uint16_t l) {
    *(unsigned char *) (p + 1) = (unsigned char) ((l >> 0) & 0xff);
    *(unsigned char *) (p + 0) = (unsigned char) ((l >> 8) & 0xff);
}

uint32_t read_u16(const char *p) {
    uint16_t res;
    res = *(const unsigned char *) (p + 0);
    res = *(const unsigned char *) (p + 1) + (res << 8);
    return res;
}

int64_t getnowtime_ms() {
    struct timeval tv = {0};
    gettimeofday(&tv, nullptr);
    return 1000 * tv.tv_sec + tv.tv_usec / 1000;
}
}
