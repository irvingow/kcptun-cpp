//
// Created by lwj on 2020/2/3.
//

#ifndef TCPTUN_TCPTUN_COMMON_H
#define TCPTUN_TCPTUN_COMMON_H
#include <string>
#include <netinet/in.h>

namespace kcptunnel {

typedef struct {
  sockaddr_in addr_;
  socklen_t slen_;
  int socket_fd_;
  bool isclient_;
} connection_info_t;

typedef struct {
  std::string ip;
  int32_t port;
} ip_port_t;

enum SocketType {
  TCP = 0,
  UDP
};

int32_t AddEvent2Epoll(const int32_t &epoll_fd, const int32_t &fd, const uint32_t &events);

int set_non_blocking(const int32_t &fd);

int new_listen_socket(const std::string &ip, const size_t &port, int &fd, SocketType socketType);

int new_connected_socket(const std::string &remote_ip, const size_t &remote_port, int &fd, SocketType socketType);

int32_t kcptunnel_init(const std::string &remote_ip,
             const int32_t &remote_port,
             const std::string &local_listen_ip,
             const int32_t &local_listen_port,
             int32_t &epoll_fd,
             int32_t &local_listen_fd,
             int32_t &remote_connected_fd,
             int32_t &timerfd);

void write_u32(char *p, uint32_t l);

void write_u16(char *p, uint16_t l);

uint32_t read_u32(const char *p);

uint32_t read_u16(const char *p);

int64_t getnowtime_ms();

}

#endif //TCPTUN_TCPTUN_COMMON_H
