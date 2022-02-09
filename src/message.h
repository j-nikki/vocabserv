#pragma once

#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "jutil.h"

//
// STRING
//

struct string {
    const char *p;
    std::size_t n;
    constexpr JUTIL_INLINE auto operator<=>(const string &rhs) const noexcept
    {
        return std::string_view{p, n} <=> rhs;
    }
    constexpr JUTIL_INLINE operator std::string_view() const noexcept { return {p, n}; }
    constexpr JUTIL_INLINE std::string_view sv() const noexcept { return {p, n}; }
};

//
// STARTLINE
//

enum class method { GET, HEAD, POST, PUT, DELETE_, CONNECT, OPTIONS, TRACE, PATH, err };
enum class version { http10, http11, err };
struct start {
    string tgt;
    version ver;
    method mtd;
};

void parse_start(char *const f, char *const l, start &s) noexcept;

//
// HEADERS
//

struct headers {
    struct entry {
        string first, second;
    };
    static constexpr auto defcap = 64_uz;

    //
    // SPAN
    //
    [[nodiscard]] JUTIL_INLINE const entry *begin() const noexcept { return buf_.get(); }
    [[nodiscard]] JUTIL_INLINE const entry *end() const noexcept { return buf_.get() + n_; }
    [[nodiscard]] JUTIL_INLINE entry *begin() noexcept { return buf_.get(); }
    [[nodiscard]] JUTIL_INLINE entry *end() noexcept { return buf_.get() + n_; }

    //
    // MODIFICATION
    //
    constexpr JUTIL_INLINE void clear() noexcept { n_ = 0; }
    void grow();
    void reserve(const char *const kf, const char *const kl, const char *const vf,
                 const char *const vl);

    //
    // KEY GET
    //
    [[nodiscard]] JUTIL_INLINE std::string_view get(const std::string_view keyuc) const
    {
        const auto it = sr::find(begin(), end(), keyuc, L(std::string_view(x.first.p, x.first.n)));
        return (it == end()) ? throw std::out_of_range{"headers::get"} : it->second;
    }
    [[nodiscard]] JUTIL_INLINE std::string_view get(const std::string_view keyuc,
                                                    const std::string_view def) const noexcept
    {
        const auto it = sr::find(begin(), end(), keyuc, L(std::string_view(x.first.p, x.first.n)));
        return (it == end()) ? def : it->second;
    }
    template <std::size_t N>
    [[nodiscard]] JUTIL_INLINE std::string_view get(const char (&key)[N]) const
    {
        char uc[N - 1];
        std::transform(key, key + (N - 1), uc, FREF(toupper));
        return get(uc);
    }
    template <std::size_t N>
    [[nodiscard]] JUTIL_INLINE std::string_view get(const char (&key)[N],
                                                    const std::string_view def) const noexcept
    {
        char uc[N - 1];
        std::transform(key, key + (N - 1), uc, FREF(toupper));
        return get(uc, def);
    }

    std::unique_ptr<entry[]> buf_ = std::make_unique_for_overwrite<entry[]>(defcap);
    std::size_t n_ = 0_uz, cap_ = defcap;
};

//! @brief Parses given HTTP message header; [f:l) must be writable; l must point to CRLFCR and be writable
//! @param f Pointer to the beginning of the message
//! @param l Pointer to the end of the message
//! @param msg Object to represent parsed message
void parse_header(char *f, char *const l, struct message &msg);
void print_header(const struct message &msg);

//
// MESSAGE
//

struct message {
    start strt;
    headers hdrs;
    string body;
};