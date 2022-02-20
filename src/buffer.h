#pragma once

#include <memory>
#include <string>

#include "format.h"
#include "jutil.h"

#include "lmacro_begin.h"

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
    void grow(std::size_t n);

  private:
    template <bool Append = false, class... Args>
    JUTIL_INLINE void Put(auto sz, auto fput, Args &&...args)
    {
        const auto base  = Append ? n_ : 0;
        const auto new_n = base + sz;
        if (new_n > cap_) [[unlikely]]
            grow<Append>(new_n);
        n_ = static_cast<std::size_t>(fput(&buf_[base], static_cast<Args &&>(args)...) - &buf_[0]);
    }

  public:
    template <bool Append = false, class... Args>
    JUTIL_INLINE std::size_t put(Args &&...args)
    {
        const auto sz = format::maxsz(static_cast<Args &&>(args)...);
        Put(
            sz,
            [](char *s, Args &&...args) {
                return format::format(s, static_cast<Args &&>(args)...);
            },
            static_cast<Args &&>(args)...);
        return n_;
    }

    std::unique_ptr<char[]> buf_ = std::make_unique_for_overwrite<char[]>(defcap);
    std::size_t n_ = 0, cap_ = defcap;
};

#include "lmacro_end.h"
