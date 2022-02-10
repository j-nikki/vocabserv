#pragma once

#include <filesystem>
#include <memory>
#include <spdlog/logger.h>

#include "jutil.h"

struct vocab {
    bool init(const std::filesystem::path &path);
    std::unique_ptr<char[]> buf;
    std::size_t nbuf;
};

extern std::shared_ptr<spdlog::logger> g_log;
extern vocab g_vocab;
