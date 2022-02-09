#pragma once

#include <filesystem>
#include <memory>
#include <spdlog/logger.h>

#include "jutil.h"

struct vocab {
    JUTIL_INLINE bool init(const std::filesystem::path &path)
    {
#ifdef WIN32
        const auto file = _wfopen(path.c_str(), L"rb");
#else
        const auto file = fopen(path.c_str(), "rb");
#endif
        if (!file)
            return false;
        DEFER[=] { fclose(file); };
        fseek(file, 0, SEEK_END);
        const auto sz = static_cast<std::size_t>(ftell(file));
        fseek(file, 0, SEEK_SET);
        buf  = std::make_unique_for_overwrite<char[]>(sz);
        nbuf = static_cast<std::size_t>(fread(buf.get(), sizeof(char), sz, file));
        return true;
    }
    std::unique_ptr<char[]> buf;
    std::size_t nbuf;
};

extern std::shared_ptr<spdlog::logger> g_log;
extern vocab g_vocab;
