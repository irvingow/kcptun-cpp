//
// Created by lwj on 2019/9/26.
//

#ifndef UDPTUN_PARSE_CONFIG_H
#define UDPTUN_PARSE_CONFIG_H

#include <noncopyable.h>
#include <cstdint>
#include <string>

struct system_config_t {
  explicit system_config_t(const std::string& config_file_path);
  int32_t parse_config_json(const std::string& config_file_path);
  int32_t BUF_SIZE;
  std::string listen_ip;
  int32_t listen_port;
  std::string remote_ip;
  int32_t remote_port;
  bool parse_flag;
};

///Meyers' Singleton
class SystemConfig : public noncopyable {
 public:
  static SystemConfig *GetInstance(const std::string& config_file_path);
  const system_config_t* system_config(){
      return &system_config_;
  }
 protected:
  explicit SystemConfig(const std::string& config_file_path);
  ~SystemConfig() = default;
  system_config_t system_config_;
};

#endif //UDPTUN_PARSE_CONFIG_H
