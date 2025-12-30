#pragma once

#include <string>
#include <mutex>
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

class Config {
public:
    static Config& GetInstance() {
        static Config instance;
        return instance;
    }

    bool Load(const std::string& path) {
        std::lock_guard<std::mutex> lock(mtx_);
        try {
            std::ifstream f(path);
            if (!f.is_open()) {
                spdlog::error("Config file not found: {}", path);
                return false;
            }
            data_ = nlohmann::json::parse(f);
            spdlog::info("Config loaded from {}", path);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Config parse error: {}", e.what());
            return false;
        }
    }

    std::string GetString(const std::string& key, const std::string& default_val = "") {
        std::lock_guard<std::mutex> lock(mtx_);
        try {
            if (data_.contains(key)) {
                return data_[key].get<std::string>();
            }
            // Support dot notation: "mysql.host" -> "/mysql/host"
            std::string path = key;
            std::replace(path.begin(), path.end(), '.', '/');
            if (path[0] != '/') path = "/" + path;
            
            nlohmann::json::json_pointer ptr(path);
            if (data_.contains(ptr)) {
                 return data_[ptr].get<std::string>();
            }
        } catch (...) {}
        return default_val;
    }

    int GetInt(const std::string& key, int default_val = 0) {
        std::lock_guard<std::mutex> lock(mtx_);
        try {
            if (data_.contains(key)) {
                return data_[key].get<int>();
            }
            // Support dot notation
            std::string path = key;
            std::replace(path.begin(), path.end(), '.', '/');
            if (path[0] != '/') path = "/" + path;
            
            nlohmann::json::json_pointer ptr(path);
            if (data_.contains(ptr)) {
                 return data_[ptr].get<int>();
            }
        } catch (...) {}
        return default_val;
    }

    std::vector<std::string> GetStringList(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<std::string> res;
        try {
            // Check direct key
            if (data_.contains(key) && data_[key].is_array()) {
                for (auto& el : data_[key]) {
                    res.push_back(el.get<std::string>());
                }
                return res;
            }

            // Check dot notation
            std::string path = key;
            std::replace(path.begin(), path.end(), '.', '/');
            if (path[0] != '/') path = "/" + path;
            
            nlohmann::json::json_pointer ptr(path);
            if (data_.contains(ptr) && data_[ptr].is_array()) {
                 for (auto& el : data_[ptr]) {
                    res.push_back(el.get<std::string>());
                }
            }
        } catch (...) {}
        return res;
    }

private:
    Config() = default;
    std::mutex mtx_;
    nlohmann::json data_;
};
