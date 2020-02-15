#ifndef KCPTUN_KCPTUNNEL_CONNECTION_MANAGER_H
#define KCPTUN_KCPTUNNEL_CONNECTION_MANAGER_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include "kcptunnel_common.h"

namespace kcptunnel {

class ConnectionManager {
 public:
  ConnectionManager(const int32_t &local_listen_fd, ip_port_t ip_port, void *user_data);
  ///only for kcptunnel client
  int32_t HandleNewConnection();
  int32_t RecvDataFromPeer();
  int32_t RecvDataFromOutside(const int32_t &readable_fd);
  bool ExistConnfd(const int32_t& connection_fd){
      return outside_connectionfd_2connid_.count(connection_fd);
  }
 private:
  int32_t SendDataToRemote();
 private:
  const int16_t header_len_ = 6;
 private:
  ///just for kcptunnel client, kcptunnel server does not get data from socket
  int32_t local_listen_fd_;
  ///remote server info
  ///for tcptun_client remote server info is the info of tcptun server
  ///for tcptun_server remote server info is the info of another outside server
  ip_port_t remote_server_info_;
  void *user_data_;
  char recv_buf_[4096] = {};
  int32_t recv_len_ = 0;
  ///outside connections, for tcptun_client outside connections are connections from its clients
  ///for tcptun_server outside connections are connections from its server
  ///for both client and server value is conn_id that identify the connection
  std::unordered_map<int32_t, uint32_t> outside_connectionfd_2connid_;
  ///key is conn_id and value is the connection fd
  std::unordered_map<uint32_t, int32_t> connid2outside_connectionfd_;
};

}

#endif //KCPTUN_KCPTUNNEL_CONNECTION_MANAGER_H
