#pragma once

#include <string>

#ifdef _MSC_VER
#include <format>
#else
#define DFMT_CONSTEVAL /* GCC fix */
#include <fmt/format.h>
#endif

#include "jutil.h"

struct buffer {
    static constexpr auto defcap = 4096_uz;

    //
    // DATA RETRIEVAL
    //
    [[nodiscard]] constexpr JUTIL_INLINE std::size_t size() const noexcept { return n_; }
    [[nodiscard]] JUTIL_INLINE const char *data() const noexcept { return buf_.get(); }

    //
    // MODIFICATION
    //
    constexpr JUTIL_INLINE void clear() noexcept { n_ = 0; }
    template <bool Copy>
    void grow(std::size_t n)
    {
        const auto new_cap = std::bit_ceil(n);
        auto new_mem       = std::make_unique_for_overwrite<char[]>(new_cap);
        if constexpr (Copy)
            std::copy_n(buf_.get(), n_, new_mem.get());
        buf_.reset(new_mem.release());
        cap_ = new_cap;
    }

  private:
    template <bool Append = false>
    JUTIL_INLINE void Put(auto sz, auto fput)
    {
        const auto new_n = Append ? n_ + sz : sz;
        if (new_n > cap_) [[unlikely]]
            grow<Append>(new_n);
        fput(&buf_[Append ? n_ : 0]);
        n_ = new_n;
    }

  public:
    JUTIL_INLINE void put(const std::string &str) { Put(str.size(), L(str.copy(x, str.size()), &)); }

    template <bool Append = false, class Fmt, class... Args>
    requires(!std::is_same_v<std::remove_cvref_t<Fmt>, std::string>) //
        JUTIL_INLINE void put(Fmt &&fmt, Args &&...args)
    {
#ifdef _MSC_VER
        Put(std::formatted_size(fmt, args...), L((std::format_to(x, fmt, args...)), &));
#else
        // TODO: Remove. Sadly the fmtlib compiletime-errors on format_string construction, and only
        // MS stdlib has fmt as of now ('22 Feb).
        put(fmt::vformat(fmt, fmt::make_format_args(static_cast<Args &&>(args)...)));
#endif
    }

    std::unique_ptr<char[]> buf_ = std::make_unique_for_overwrite<char[]>(defcap);
    std::size_t n_ = 0, cap_ = defcap;
};