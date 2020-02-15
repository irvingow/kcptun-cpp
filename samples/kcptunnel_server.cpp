#include "connection_manager.h"
#include "kcptunnel_common.h"
#include "ikcp.h"
#include "parse_config.h"
#include "fec_manager.h"
#include <glog/logging.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>

int udpout(const char *buf, int len, ikcpcb *kcp, void *user) {
    auto fec_encoder_manager = reinterpret_cast<kcptunnel::FecEncodeManager *> (user);
    return fec_encoder_manager->Input(buf, len);
}

void run(int32_t epoll_fd,
         int32_t local_listen_fd,
         int32_t kcp_update_timer_fd,
         const kcptunnel::ip_port_t &ip_port) {
    char recv_buf[4096] = {0};
    int32_t recv_len = 0;
    const int32_t max_events = 64;
    struct epoll_event events[max_events];
    FecDecode fec_decoder(10000);
    std::shared_ptr<kcptunnel::connection_info_t> sp_conn(new kcptunnel::connection_info_t);
    sp_conn->socket_fd_ = local_listen_fd;
    sp_conn->isclient_ = false;
    std::shared_ptr<FecEncode> sp_fec_encode(new FecEncode(2, 1, 10));
    kcptunnel::FecEncodeManager fec_encode_manager(sp_conn, sp_fec_encode);
    ikcpcb *kcp = ikcp_create(0x11112222, (void *) &fec_encode_manager);
    kcp->output = udpout;
    std::shared_ptr<kcptunnel::ConnectionManager>
        sp_conn_manager(new kcptunnel::ConnectionManager(local_listen_fd, ip_port, (void *) kcp));
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, max_events, -1);
        if (nfds < 0) {
            if (errno != EINTR) {
                LOG(ERROR) << "epoll_wait return error:" << strerror(errno);
                break;
            }
        }
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == local_listen_fd) {
                ///获得从server端的数据
                bzero(recv_buf, sizeof(recv_buf));
                recv_len = recvfrom(local_listen_fd,
                                    recv_buf,
                                    sizeof(recv_buf),
                                    0,
                                    (sockaddr *) &sp_conn->addr_,
                                    &sp_conn->slen_);
                LOG(INFO) << "recv data from kcptunnel client recv_len:" << recv_len;
                if (recv_len < 0) {
                    LOG(ERROR) << "failed to recv data from remote server error:%s" << strerror(errno);
                    continue;
                }
                ///we should first send to data to fec_decoder for decoding
                auto len = fec_decoder.Input(recv_buf, recv_len);
                while (len > 0) {
                    ///说明数据已经被全部解码完成,需要发送给kcp处理
                    char *recvbuf = (char *) malloc(len + 1);
                    if (recvbuf == nullptr) {
                        LOG(ERROR) << "failed to call malloc";
                        break;
                    }
                    bzero(recvbuf, len + 1);
                    auto ret = fec_decoder.Output(recvbuf, len);
                    if (ret < 0) {
                        LOG(ERROR) << "failed to get decoded data from fec_decoder";
                        free(recvbuf);
                        break;
                    }
                    LOG(INFO) << "success call decode data from server len:" << len << " data:" << recvbuf + 30;
                    if (auto temp = ikcp_input(kcp, recvbuf, len) < 0) {
                        len = ret;
                        LOG(WARNING) << "ikcp_input error:" << temp;
                        free(recvbuf);
                        continue;
                    }
                    len = ret;
                    free(recvbuf);
                }

            } else if (events[i].data.fd == kcp_update_timer_fd) {
                ///we need to call ikcp_update
                auto millisec = kcptunnel::getnowtime_ms();
                ikcp_update(kcp, millisec);
                auto temp_ret = sp_fec_encode->FecEncodeUpdateTime(millisec);
                ///if fec_encode have timeout data, we just flush out timeout data
                if (temp_ret > 0)
                    fec_encode_manager.FlushUnEncodedData();
                ///maybe kcp has prepared data for us, so we call RecvDataFromPeer
                auto new_fd = sp_conn_manager->RecvDataFromPeer();
                if (new_fd <= 0 || sp_conn_manager->ExistConnfd(new_fd))
                    continue;
                LOG(INFO) << "establish new connection to server new sockfd:" << new_fd;
                kcptunnel::AddEvent2Epoll(epoll_fd, new_fd, EPOLLIN);
            } else {
                ///recv data from outside
                sp_conn_manager->RecvDataFromOutside(events[i].data.fd);
            }
        }
    }
}

int32_t init(const std::string &config_path) {
    SystemConfig *instance = SystemConfig::GetInstance(config_path);
    auto system_config = instance->system_config();
    if (!system_config->parse_flag) {
        LOG(ERROR) << "failed to parse config file";
        return -1;
    }
    const std::string local_ip = system_config->listen_ip;
    const size_t local_port = system_config->listen_port;
    const std::string remote_ip = system_config->remote_ip;
    const size_t remote_port = system_config->remote_port;
    int32_t epoll_fd = -1, local_listen_fd = -1, kcp_update_timer_fd = -1;
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG(ERROR) << "create epoll failed error:" << strerror(errno);
        return -1;
    }
    ///创建本地监听的local_listen_fd,同时将其加入epoll监听池中
    auto ret = new_listen_socket(local_ip, local_port, local_listen_fd, kcptunnel::UDP);
    if (ret < 0) {
        LOG(ERROR) << "failed to new_listen_socket error:" << strerror(errno);
        close(epoll_fd);
        return -1;
    }
    ret = kcptunnel::AddEvent2Epoll(epoll_fd, local_listen_fd, EPOLLIN);
    if (ret != 0) {
        close(local_listen_fd);
        close(epoll_fd);
        LOG(INFO) << "add local_udp_listen_fd to epoll failed, error:" << strerror(errno);
        return -1;
    }
    ///create timerfd
    struct itimerspec temp = {0, 0, 0, 0};
    temp.it_value.tv_sec = 1;
    temp.it_value.tv_nsec = 0;
    temp.it_interval.tv_nsec = 50000000;
    kcp_update_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (kcp_update_timer_fd == -1) {
        LOG(ERROR) << "failed to call timerfd_create error:" << strerror(errno);
        return -1;
    }
    if (timerfd_settime(kcp_update_timer_fd, TFD_TIMER_ABSTIME, &temp, nullptr) == -1) {
        LOG(ERROR) << "failed to call timerfd_settime error:" << strerror(errno);
        return -1;
    }
    ret = kcptunnel::AddEvent2Epoll(epoll_fd, kcp_update_timer_fd, EPOLLIN);
    if (ret != 0) {
        close(local_listen_fd);
        close(kcp_update_timer_fd);
        close(epoll_fd);
        LOG(INFO) << "add local_udp_listen_fd failed, error:" << strerror(errno);
        return -1;
    }
    kcptunnel::ip_port_t ip_port;
    ip_port.ip = remote_ip;
    ip_port.port = remote_port;
    run(epoll_fd, local_listen_fd, kcp_update_timer_fd, ip_port);
}

int main(int argc, char *argv[]) {
    google::InitGoogleLogging("INFO");
    FLAGS_logtostderr = true;
    if (argc != 2) {
        LOG(ERROR) << "usage:" << argv[0] << " config_json_path";
        return 0;
    }
    const std::string config_file_path(argv[1]);
    init(config_file_path);
    return 0;
}