﻿#include <boost/asio.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/preprocessor/cat.hpp>
#include <chrono>
#include <memory>
#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "format.h"
#include "jutil.h"
#include "message.h"
#include "vocabserv.h"
#include <res.h>

#include "lmacro_begin.h"

#define KEEP_ALIVE_SECS 5

namespace sc = std::chrono;
namespace ba = boost::asio;

[[nodiscard]] JUTIL_INLINE const std::string_view &get_mimetype(const std::string_view uri) noexcept
{
    static constexpr std::string_view exts[]{".js", ".css"};
    static constexpr std::string_view types[]{"text/javascript", "text/css", "text/html"};
    return types[find_if_unrl_idx(exts, L(uri.ends_with(x), =))];
}

#define STATIC_SV(X, ...)                                                                          \
    [__VA_ARGS__]() -> const std::string_view & {                                                  \
        static constexpr std::string_view BOOST_PP_CAT(x, __LINE__) = X;                           \
        return BOOST_PP_CAT(x, __LINE__);                                                          \
    }()

struct gc_res {
    const std::string_view &type = STATIC_SV(""), &hdr = STATIC_SV("");
};

template <auto X>
[[nodiscard]] JUTIL_INLINE constexpr auto &as_static() noexcept
{
    return X;
}

[[nodiscard]] JUTIL_INLINE gc_res serve_api(const string &uri, buffer &body) noexcept
{
    using namespace std::string_view_literals;
    if (uri == "vocabVer") {
        body.put(std::string_view{"1"});
        return {STATIC_SV("text/plain")};
    }
    if (uri == "vocab") {
        body.put(std::string_view{g_vocab.buf.get(), g_vocab.nbuf});
        return {STATIC_SV("text/plain"), STATIC_SV("content-encoding: gzip\r\n")};
    }
    return {};
}

[[nodiscard]] JUTIL_INLINE gc_res get_content(const string &uri, buffer &body) noexcept
{
    using namespace std::string_view_literals;
    if (uri.sv().starts_with("/api/"))
        return serve_api(uri.substr(5), body);
    if (const auto idx = find_unrl_idx(res::names, uri); idx < res::names.size()) {
        body.put(res::contents[idx]);
        return {get_mimetype(uri), STATIC_SV("content-encoding: gzip\r\n")};
    }
    return {};
}

constexpr std::string_view nf1 = "<!DOCTYPE html><meta charset=utf-8><title>Error 404 (Not "
                                 "Found)</title><p><b>404</b> Not Found.<p>The resource <code>",
                           nf2 = "</code> was not found.";

struct escaped {
    escaped(const std::string_view sv) : f{sv.data()}, l{sv.data() + sv.size()} {}
    constexpr JUTIL_INLINE std::size_t size() const noexcept
    {
        return static_cast<std::size_t>(l - f);
    }
    const char *const f, *const l;
};

template <class It>
It escape(const char *f, const char *const l, It d_f) noexcept
{
#define ESC_copystr(S) std::copy_n(S, sizeof(S) - 1, d_f)
    for (; f != l; ++f) {
        switch (const char c = *f) {
        case '<':
            d_f = ESC_copystr("&lt;");
            break;
        case '>':
            d_f = ESC_copystr("&gt;");
            break;
        case '&':
            d_f = ESC_copystr("&amp;");
            break;
        default:
            *d_f++ = c;
        }
    }
    return d_f;
}

template <>
struct format::formatter<escaped> {
    static char *format(char *d_f, const escaped &e) noexcept { return escape(e.f, e.l, d_f); }
    static std::size_t maxsz(const escaped &e) noexcept { return e.size() * 5; }
};

//! @brief Writes a response message serving a given request message
//! @param rq Request message to serve
//! @param rs Response message for given request
void serve(const message &rq, buffer &rs, buffer &body)
{
    if (rq.strt.mtd == method::err)
        goto badreq;
    if (rq.strt.ver == version::err)
        goto badver;

    switch (rq.strt.mtd) {
    case method::GET: {
        if (const auto [type, hdr] = get_content(rq.strt.tgt, body); !type.empty()) {
            rs.put("HTTP/1.1 200 OK\r\nconnection: keep-alive\r\ncontent-type: ", type,
                   "; charset=UTF-8\r\ndate: ", format::hdr_time{}, //
                   "\r\ncontent-length: ", body.size(),
                   "\r\nkeep-alive: timeout=" BOOST_STRINGIZE(KEEP_ALIVE_SECS), //
                   "\r\n", hdr,                                                 //
                   "\r\n", std::string_view{body.data(), body.size()});
        } else {
            const escaped res = rq.strt.tgt.sv().substr(0, 100);
            rs.put("HTTP/1.1 404 Not Found\r\n"
                   "content-type: text/html; charset=UTF-8\r\n"
                   "content-length:",
                   nf1.size() + nf2.size() + res.size(), //
                   "\r\ndate: ", format::hdr_time{},     //
                   "\r\n\r\n", nf1, res, nf2);
        }
        return;
    }
    default:;
    }
badreq:
    rs.put("400 Bad Request\r\n\r\n\r\n");
    return;
badver:
    rs.put("505 HTTP Version Not Supported\r\n\r\n\r\n");
}

DBGSTMNT(static int ncon = 0;)

inline constexpr auto coro_hdlr = ba::experimental::as_tuple(ba::use_awaitable);
ba::awaitable<void> handle_connection(ba::ip::tcp::socket soc_)
{
    using namespace ba::experimental::awaitable_operators;

    DBGEXPR(const int id_ = ncon++);
    DBGEXPR(printf("con#%d: accepted\n", id_));

    // Read into buffer
    ba::steady_timer to_{soc_.get_executor(), sc::seconds(KEEP_ALIVE_SECS)};
    std::vector<char> rq_;
    const auto res =
        co_await(ba::async_read_until(soc_, ba::dynamic_buffer(rq_), "\r\n\r\n", coro_hdlr) ||
                 to_.async_wait(coro_hdlr));
    if (res.index() == 1)
        co_return; // timeout

    // Read request
    const auto [rdec, rdn] = std::get<0>(res);
    if (rdec) {
        if (rdec != ba::error::eof)
            g_log.print(std::string_view{rdec.category().name()}, ": ", rdec.value(), ": ",
                        std::string_view{rdec.message()});
        co_return;
    }

    // Handle request & build response
    message msg_;
    parse_header(rq_.data(), rq_.data() + (rdn - 4), msg_);
    DBGEXPR(printf("vvv con#%d: received message with the header:\n", id_));
    DBGEXPR(print_header(msg_));
    DBGEXPR(printf("^^^\n"));
    // determining message length (after CRLFCRLF):
    // https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
    buffer rs_;
    buffer rs_body_;
    serve(msg_, rs_, rs_body_);
    rq_.clear();

    // Write response
    const auto [wrec, _] =
        co_await ba::async_write(soc_, ba::buffer(rs_.data(), rs_.size()), coro_hdlr);
    if (wrec) {
        g_log.print(std::string_view{wrec.category().name()}, ": ", wrec.value(), ": ",
                    std::string_view{wrec.message()});
        co_return;
    }

    DBGEXPR(printf("con#%d: write end\n", id_));
}

void accept_loop(auto &ioc, auto &ac, auto &soc)
{
    ac.async_accept(soc, [&](auto ec) {
        if (!ec)
            ba::co_spawn(ioc, handle_connection(std::move(soc)), ba::detached);
        accept_loop(ioc, ac, soc);
    });
}

void run_server(const ba::ip::tcp::endpoint &ep)
{
    ba::io_context ioc{1};
    ba::ip::tcp::acceptor ac{ioc, ep};
    ba::ip::tcp::socket soc{ioc};
    accept_loop(ioc, ac, soc);
    ioc.run();
}
