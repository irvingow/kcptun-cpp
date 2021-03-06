//
// Created by lwj on 2019/10/11.
//

#include <cstring>
#include <algorithm>
#include "fec_encode.h"
#include "libfec_random_generator.h"
#include "common.h"

FecEncode::FecEncode(const int32_t &data_pkg_num, const int32_t &redundant_pkg_num, const uint32_t &timeout)
    : inside_timer_(0),
      cur_data_pkgs_num_(0),
      max_data_pkg_length_(0),
      ready_for_fec_output_(false),
      data_pkg_num_(data_pkg_num),
      redundant_pkg_num_(redundant_pkg_num),
      timeout_time_(timeout) {
    RandomNumberGenerator *rg = RandomNumberGenerator::GetInstance();
    auto ret = rg->GetRandomNumberU16(seq);
    if (ret < 0) {
        seq = 1;
    }
    ///65521 is the max primer number in the range of 0-65535
    if (seq >= 65521)
        seq = 1;
    data_pkgs_length_.resize(data_pkg_num + redundant_pkg_num);
    data_pkgs_.resize(data_pkg_num + redundant_pkg_num);
    for (auto &data_pkg_length : data_pkgs_length_)
        data_pkg_length = 0;
    for (int i = 0; i < data_pkgs_.size(); ++i)
        data_pkgs_[i] = nullptr;
}

FecEncode::~FecEncode() {
    std::lock_guard<std::mutex> lck(data_pkgs_mutex_);
    ResetDataPkgs();
}

int32_t FecEncode::Input(const char *input_data_pkg, int32_t length) {
    uint64_t time_temp = inside_timer_;
    newest_update_time_ = time_temp;
    std::lock_guard<std::mutex> lck(data_pkgs_mutex_);
    if (cur_data_pkgs_num_ == data_pkg_num_)
        return -1;
    if (input_data_pkg == nullptr || length <= 0 || length > 65535)
        return -2;
    if (cur_data_pkgs_num_ == 0) {
        ready_for_fec_output_ = false;
        max_data_pkg_length_ = 0;
        ResetDataPkgs();
    }
    ///因为实际上我们添加的fec头部是不进入fec编码的
    max_data_pkg_length_ = std::max(length, static_cast<int32_t >(max_data_pkg_length_));
    length += fec_encode_head_length_;
    data_pkgs_[cur_data_pkgs_num_] = (char *) malloc((length + 1) * sizeof(char));
    data_pkgs_length_[cur_data_pkgs_num_] = length;
    bzero(data_pkgs_[cur_data_pkgs_num_], length + 1);
    write_u32(data_pkgs_[cur_data_pkgs_num_], unique_header_);
    write_u16(data_pkgs_[cur_data_pkgs_num_] + 4, seq);
    write_u16(data_pkgs_[cur_data_pkgs_num_] + 6, length - fec_encode_head_length_);
    data_pkgs_[cur_data_pkgs_num_][8] = (unsigned char) data_pkg_num_;
    data_pkgs_[cur_data_pkgs_num_][9] = (unsigned char) redundant_pkg_num_;
    ///注意这里索引不能用0,用0的话可能导致在不注意的情况下字符串数据被截断,也就是将0作为结束的标志了,所以改成从1开始
    data_pkgs_[cur_data_pkgs_num_][10] = (unsigned char) (cur_data_pkgs_num_ + 1);
    memcpy(data_pkgs_[cur_data_pkgs_num_] + fec_encode_head_length_, input_data_pkg, length - fec_encode_head_length_);
    cur_data_pkgs_num_++;
    if (cur_data_pkgs_num_ == data_pkg_num_) {
        char **data = nullptr;
        data = (char **) malloc((data_pkg_num_ + redundant_pkg_num_) * sizeof(char *));
        for (int32_t i = 0; i < data_pkg_num_ + redundant_pkg_num_; ++i) {
            ///注意这里必须要为每个数据包重新分配更大的空间,否则空间不够的情况下有些编码的数据会丢失
            data[i] = (char *) malloc((max_data_pkg_length_ + fec_encode_head_length_ + 1) *
                sizeof(char));
            bzero(data[i], (max_data_pkg_length_ + fec_encode_head_length_ + 1) * sizeof(char));
            if (i < data_pkg_num_) {
                memcpy(data[i], data_pkgs_[i], data_pkgs_length_[i]);
                free(data_pkgs_[i]);
                data_pkgs_[i] = nullptr;
            } else {
                write_u32(data[i], unique_header_);
                write_u16(data[i] + 4, seq);
                write_u16(data[i] + 6, max_data_pkg_length_);
                data[i][8] = (unsigned char) data_pkg_num_;
                data[i][9] = (unsigned char) redundant_pkg_num_;
            }
            data[i][10] = (unsigned char) (i + 1);
            ///因为实际上我们添加的fec头部是不进入fec编码的
            data[i] += fec_encode_head_length_;
        }
        rs_encode2(data_pkg_num_, data_pkg_num_ + redundant_pkg_num_, data, max_data_pkg_length_);
        for (int32_t i = 0; i < data_pkg_num_ + redundant_pkg_num_; ++i) {
            data[i] -= fec_encode_head_length_;
            data_pkgs_[i] = data[i];
        }
        for (int32_t i = 0; i < data_pkg_num_ + redundant_pkg_num_; ++i) {
            data_pkgs_length_[i] = max_data_pkg_length_ + fec_encode_head_length_;
        }
        free(data);
        ready_for_fec_output_ = true;
        seq++;
        ///65521 is the max prime number smaller than the max number in uint16_t
        if (seq >= 65521)
            seq = 1;
        return 1;
    }
    return 0;
}

int32_t FecEncode::Output(std::vector<char *> &data_pkgs, std::vector<int32_t> &data_pkgs_length) {
    std::lock_guard<std::mutex> lck(data_pkgs_mutex_);
    if (!ready_for_fec_output_) {
        return -1;
    }
    data_pkgs = data_pkgs_;
    data_pkgs_length = data_pkgs_length_;
    cur_data_pkgs_num_ = 0;
    return 0;
}

int32_t FecEncode::FecEncodeUpdateTime(const uint64_t &cur_millsec) {
    if (inside_timer_ == 0) {
        newest_update_time_ = inside_timer_ = cur_millsec;
        return 0;
    }
    if (cur_millsec < inside_timer_)
        return -1;
    inside_timer_ = cur_millsec;
    if ((cur_millsec - newest_update_time_) < (timeout_time_ * 1000))
        return 0;
    ///means we have waited for the next data package for longer time than timeout
    return 1;
}

int32_t FecEncode::FlushUnEncodedData(std::vector<char *> &data_pkgs, std::vector<int32_t> &data_pkgs_length) {
    std::lock_guard<std::mutex> lck(data_pkgs_mutex_);
    if (cur_data_pkgs_num_ == data_pkg_num_)
        return -1;
    data_pkgs.resize(cur_data_pkgs_num_);
    data_pkgs_length.resize(cur_data_pkgs_num_);
    for (int i = 0; i < cur_data_pkgs_num_; ++i) {
        data_pkgs[i] = data_pkgs_[i] + fec_encode_head_length_ - sizeof(unique_header_);
        ///we just add reverse unique header before the head of unencoded data package
        write_u32_r(data_pkgs[i], unique_header_);
        data_pkgs_length[i] = data_pkgs_length_[i] - fec_encode_head_length_ + sizeof(unique_header_);
    }
    cur_data_pkgs_num_ = 0;
    return 0;
}

void FecEncode::ResetDataPkgs() {
    for (auto &data_pkg_length : data_pkgs_length_)
        data_pkg_length = 0;
    for (int i = 0; i < data_pkgs_.size(); ++i) {
        free(data_pkgs_[i]);
        data_pkgs_[i] = nullptr;
    }
}

