//
// Created by lwj on 2019/9/26.
//

#include "parse_config.h"
#include <glog/logging.h>
#include <fstream>
#include <rapidjson/document.h>

system_config_t::system_config_t(const std::string &config_file_path) {
    auto ret = parse_config_json(config_file_path);
    if (ret < 0) {
        LOG(ERROR) << "failed to parse config json";
        BUF_SIZE = 0;
        listen_ip.clear();
        remote_ip.clear();
        listen_port = 0;
        remote_port = 0;
        parse_flag = false;
    }
    else{
        parse_flag = true;
    }
}

int32_t system_config_t::parse_config_json(const std::string &config_file_path) {
    std::ifstream is(config_file_path.c_str());
    std::string contents((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    if (contents.empty()) {
        LOG(ERROR) << "failed to open config_json_file path:" << config_file_path;
        return -1;
    }
    rapidjson::Document document;
    document.Parse(contents.c_str());
    if (document.HasParseError()) {
        LOG(ERROR) << "failed to parse config_json_file contents, please check file path or file format";
        return -1;
    }
    if (!document.HasMember("BUF_SIZE")) {
        LOG(ERROR) << "invalid format, BUF_SIZE must be contained";
        return -1;
    } else {
        rapidjson::Value &BUF_SIZE_json = document["BUF_SIZE"];
        BUF_SIZE = BUF_SIZE_json.GetInt();
        if (BUF_SIZE > 2048) {
            LOG(WARNING) << "BUF_SIZE:" << BUF_SIZE << " maybe too big";
        }
    }
    if (!document.HasMember("listen_ip")) {
        LOG(ERROR) << "invalid format, listen_ip must be contained";
        return -1;
    } else {
        rapidjson::Value &listen_ip_json = document["listen_ip"];
        listen_ip = std::string(listen_ip_json.GetString());
    }
    if (!document.HasMember("listen_port")) {
        LOG(ERROR) << "invalid format, listen_port be contained";
        return -1;
    } else {
        rapidjson::Value &listen_port_json = document["listen_port"];
        listen_port = listen_port_json.GetInt();
    }
    if (!document.HasMember("remote_ip")) {
        LOG(ERROR) << "invalid format, listen_port be contained";
        return -1;
    } else {
        rapidjson::Value &remote_ip_json = document["remote_ip"];
        remote_ip = std::string(remote_ip_json.GetString());
    }
    if (!document.HasMember("remote_port")) {
        LOG(ERROR) << "invalid format, listen_port be contained";
        return -1;
    } else {
        rapidjson::Value &remote_port_json = document["remote_port"];
        remote_port = remote_port_json.GetInt();
    }
}

SystemConfig::SystemConfig(const std::string &config_file_path) : system_config_(config_file_path) {}

SystemConfig* SystemConfig::GetInstance(const std::string& config_file_path) {
    static SystemConfig instance(config_file_path);
    return &instance;
}





























