#include <glog/logging.h>
#include <sys/socket.h>
#include "ikcp.h"
#include "connection_manager.h"
#include "kcptunnel_common.h"
#include "random_generator.h"

namespace kcptunnel {

ConnectionManager::ConnectionManager(const int32_t &local_listen_fd, kcptunnel::ip_port_t ip_port, void *user_data) :
    local_listen_fd_(local_listen_fd), remote_server_info_(std::move(ip_port)), user_data_(user_data) {
    auto ret = set_non_blocking(local_listen_fd_);
    if (ret < 0)
        LOG(WARNING) << "failed to call set_non_blocking to local_listen_fd:" << local_listen_fd;
}

int32_t ConnectionManager::HandleNewConnection() {
    auto new_conn_fd = accept(local_listen_fd_, nullptr, nullptr);
    if (new_conn_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        LOG(ERROR) << "tcptun client failed to call accept, error:" << strerror(errno);
        return -1;
    }
    uint32_t conn_id = 0;
    while (true) {
        conn_id = 0;
        auto ret = RandomNumberGenerator::GetInstance()->GetRandomNumberU32(conn_id);
        if (ret < 0) {
            LOG(ERROR) << "failed to call GetRandomNumberNonZero ret" << ret;
            return -2;
        }
        if (!connid2outside_connectionfd_.count(conn_id))
            break;
    }
    connid2outside_connectionfd_[conn_id] = new_conn_fd;
    outside_connectionfd_2connid_[new_conn_fd] = conn_id;
    return new_conn_fd;
}

int32_t ConnectionManager::RecvDataFromPeer() {
    auto kcp = (ikcpcb *) user_data_;
    if (kcp->stream == 0) {
        bzero(recv_buf_, sizeof(recv_buf_));
        auto ret = ikcp_recv(kcp, recv_buf_, sizeof(recv_buf_));
        ///means kcp does not have prepared data for us
        if (ret <= 0) {
            return 0;
        }
        if (ret <= header_len_) {
            LOG(ERROR) << "wrong package without kcptunnel header!";
            return -1;
        }
        recv_len_ = ret;
        auto unique_connId = read_u32(recv_buf_);
        if (!connid2outside_connectionfd_.count(unique_connId)) {
            ///only server will go this
            int32_t connected_fd = -1;
            ret = new_connected_socket(remote_server_info_.ip, remote_server_info_.port,
                                       connected_fd, kcptunnel::TCP);
            if (ret < 0) {
                LOG(ERROR) << "failed to call new_connected_socket ret:" << ret;
                return -2;
            }
            ret = set_non_blocking(connected_fd);
            if (ret < 0)
                LOG(WARNING) << "failed to call set_non_blocking on connected_fd:" << connected_fd;
            outside_connectionfd_2connid_[connected_fd] = unique_connId;
            connid2outside_connectionfd_[unique_connId] = connected_fd;
        }
        auto data_len = read_u16(recv_buf_ + sizeof(uint32_t));
        if (data_len != (recv_len_ - header_len_)) {
            LOG(ERROR) << "wrong package data_len is not equal to actual data length";
            return -1;
        }
        ret = SendDataToRemote();
        if (ret < 0)
            LOG(ERROR) << "failed to call SendDataToRemote ret:" << ret;
        return connid2outside_connectionfd_[unique_connId];
    } else if (kcp->stream == 1) {
        if (recv_len_ >= header_len_) {
            auto unique_connid = read_u32(recv_buf_);
            auto data_len = read_u16(recv_buf_ + sizeof(uint32_t));
            auto rest_len = data_len - recv_len_ + header_len_;
            auto ret = ikcp_recv(kcp, recv_buf_ + recv_len_, rest_len);
            ///means kcp does not have prepared data for us
            if (ret <= 0) {
                return 0;
            }
            recv_len_ += ret;
            if (recv_len_ == (data_len + header_len_)) {
                ret = SendDataToRemote();
                if (ret < 0)
                    LOG(ERROR) << "failed to call SendDataToRemote ret:" << ret;
            }
            return connid2outside_connectionfd_[unique_connid];
        }
        if (recv_len_ < header_len_) {
            auto ret = ikcp_recv(kcp, recv_buf_ + recv_len_, header_len_ - recv_len_);
            if (ret <= 0) {
                return 0;
            }
            recv_len_ += ret;
            if (recv_len_ < header_len_)
                return 0;
        }
        auto unique_connId = read_u32(recv_buf_);
        if (!connid2outside_connectionfd_.count(unique_connId)) {
            ///only server will go this
            int32_t connected_fd = -1;
            auto ret = new_connected_socket(remote_server_info_.ip, remote_server_info_.port,
                                            connected_fd, kcptunnel::TCP);
            if (ret < 0) {
                LOG(ERROR) << "failed to call new_connected_socket ret:" << ret;
                return -2;
            }
            ret = set_non_blocking(connected_fd);
            if (ret < 0)
                LOG(WARNING) << "failed to call set_non_blocking on connected_fd:" << connected_fd;
            outside_connectionfd_2connid_[connected_fd] = unique_connId;
            connid2outside_connectionfd_[unique_connId] = connected_fd;
        }
        auto data_len = read_u16(recv_buf_ + sizeof(uint32_t));
        if (data_len > (sizeof(recv_buf_) - header_len_)) {
            LOG(WARNING) << "too big data pkg len:" << data_len;
            return -3;
        }
        auto ret = ikcp_recv(kcp, recv_buf_ + header_len_, data_len);
        if (ret <= 0) {
            return 0;
        }
        LOG(INFO) << "prepare to send data remote";
        recv_len_ += ret;
        if (recv_len_ == (data_len + header_len_)) {
            ret = SendDataToRemote();
            if (ret < 0)
                LOG(ERROR) << "failed to call SendDataToRemote ret:" << ret;
        }
        return connid2outside_connectionfd_[unique_connId];
    }
}

int32_t ConnectionManager::RecvDataFromOutside(const int32_t &readable_fd) {
    if (!outside_connectionfd_2connid_.count(readable_fd)) {
        LOG(ERROR) << "readable_fd is not recorded:" << readable_fd;
        return -1;
    }
    bzero(recv_buf_, sizeof(recv_buf_));
    auto ret = recv(readable_fd, recv_buf_ + header_len_, sizeof(recv_buf_) - header_len_, 0);
    if (ret < 0) {
        LOG(ERROR) << "failed to call recv error" << strerror(errno);
        return -2;
    } else if (ret == 0) {
        LOG(INFO) << "outside connection closed";
        ///a closing fd will be moved by epoll, so we don't need to worry about it
        close(readable_fd);
        uint32_t conn_id = outside_connectionfd_2connid_[readable_fd];
        outside_connectionfd_2connid_.erase(readable_fd);
        connid2outside_connectionfd_.erase(conn_id);
        return 0;
    }
    LOG(INFO) << "recv data from client len:" << ret << " data:" << recv_buf_ + header_len_;
    auto data_len = static_cast<uint16_t >(ret);
    write_u32(recv_buf_, outside_connectionfd_2connid_[readable_fd]);
    write_u16(recv_buf_ + sizeof(uint32_t), data_len);
    recv_len_ = data_len + header_len_;
    auto kcp = (ikcpcb *) user_data_;
    ret = ikcp_send(kcp, recv_buf_, recv_len_);
    if (ret < 0) {
        LOG(WARNING) << "failed to call ikcp_send ret:" << ret;
    }
}

int32_t ConnectionManager::SendDataToRemote() {
    auto unique_connId = read_u32(recv_buf_);
    auto ret = send(connid2outside_connectionfd_[unique_connId], recv_buf_ + header_len_, recv_len_ - header_len_, 0);
    if (ret < 0) {
        LOG(ERROR) << "failed to call send to connected_fd:" << connid2outside_connectionfd_[unique_connId] << " error:"
                   << strerror(errno);
        return -1;
    }
    //todo need to handle the issue when ret is less than data length, but this situation will barely happen
    if (ret != (recv_len_ - header_len_)) {
        LOG(WARNING) << "failed to send all the data for fd:" << connid2outside_connectionfd_[unique_connId];
    }
    recv_len_ = 0;
    bzero(recv_buf_, sizeof(recv_buf_));
    return 0;
}

}
























