#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "jutil.h"
#include "message.h"
#include "vocabserv.h"
#include <res.h>

namespace sc = std::chrono;
namespace ba = boost::asio;

[[nodiscard]] JUTIL_INLINE std::string_view serve_api(std::string_view uri) noexcept
{
    if (uri == "vocabVer")
        return "1";
    if (uri == "vocab")
        return {g_vocab.buf.get(), g_vocab.nbuf};
    return {};
}

[[nodiscard]] constexpr JUTIL_INLINE std::string_view
get_mimetype(const std::string_view uri) noexcept
{
    constexpr std::string_view exts[]{".js", ".css"};
    constexpr std::string_view types[]{"text/javascript", "text/css", "text/html"};
    return types[find_if_unrl_idx(exts, L(uri.ends_with(x), =))];
}

struct gc_res {
    std::string_view cont{}, type{}, hdr{};
};

[[nodiscard]] JUTIL_INLINE gc_res get_content(std::string_view uri) noexcept
{
    if (uri.starts_with("/api/"))
        return {serve_api(uri.substr(5)), "text/plain", ""};
    if (const auto idx = find_unrl_idx(res::names, uri); idx < res::names.size())
        return {res::contents[idx], get_mimetype(uri), "content-encoding: gzip\r\n"};
    return {};
}

constexpr std::string_view nf1 = "<!DOCTYPE html><meta charset=utf-8><title>Error 404 (Not "
                                 "Found)</title><p><b>404</b> Not Found.<p>The resource <i>",
                           nf2 = "</i> was not found.";

//! @brief Writes a response message serving a given request message
//! @param rq Request message to serve
//! @param rs Response message for given request
void serve(const message &rq, buffer &rs)
{
    if (rq.strt.mtd == method::err)
        goto badreq;
    if (rq.strt.ver == version::err)
        goto badver;

    switch (rq.strt.mtd) {
        using enum method;
    case GET: {
        // No formatting time points on GCC ('22 Feb).
        // const auto date = sc::time_point_cast<sc::seconds>(sc::utc_clock::now());
        if (const auto [cont, type, hdr] = get_content(rq.strt.tgt); !cont.empty()) {
            rs.put("HTTP/1.1 200 OK\r\n"
                   "connection: keep-alive\r\n"
                   "content-type: {}; charset=UTF-8\r\n"
                   //"date: {:%a, %d %b %Y %T GMT}\r\n"
                   "keep-alive: timeout=5, max=999\r\n"
                   "content-length: {}\r\n"
                   "{}"
                   "\r\n"
                   "{}",
                   type, /*date,*/ cont.size(), hdr, cont);
        } else {
            rs.put("HTTP/1.1 404 Not Found\r\n"
                   "content-type: text/html; charset=UTF-8\r\n"
                   "content-length: {}\r\n"
                   //"date: {:%a, %d %b %y %t gmt}\r\n"
                   "\r\n"
                   "{}{}{}",
                   nf1.size() + nf2.size() + rq.strt.tgt.n, /*date,*/ nf1, rq.strt.tgt.sv(), nf2);
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

struct connection : std::enable_shared_from_this<connection> {
    using pointer                  = std::unique_ptr<connection>;
    using socket                   = ba::ip::tcp::socket;
    connection()                   = delete;
    connection(const connection &) = delete;
    connection(connection &&)      = delete;
    connection(socket &&soc) : soc_{std::move(soc)} {}
    socket soc_;
    std::vector<char> rq_;
    message msg_;
    buffer rs_;
    auto bound(auto f) { return std::bind_front(f, shared_from_this()); }
    void on_write(const boost::system::error_code &, std::size_t) { soc_.close(); }
    void on_read(const boost::system::error_code &ec, std::size_t n)
    {
        if (ec) {
            if (ec != ba::error::eof)
                g_log->error("{}: {}: {}\n", ec.category().name(), ec.value(),
                             ec.message().c_str());
            return;
        }
        parse_header(rq_.data(), rq_.data() + (n - 4), msg_);
        DBGEXPR(printf("vvv received message with the header:\n"));
        DBGEXPR(print_header(msg_));
        DBGEXPR(printf("^^^\n"));

        // determining message length (after CRLFCRLF):
        // https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
        serve(msg_, rs_);
        rq_.clear();
        ba::async_write(soc_, ba::buffer(rs_.data(), rs_.size()), bound(&connection::on_write));
    }
    void start()
    {
        ba::async_read_until(soc_, ba::dynamic_buffer(rq_), "\r\n\r\n",
                             bound(&connection::on_read));
    }
};

void run_server(const ba::ip::tcp::endpoint &ep)
{
    ba::io_context ioc{1};
    ba::ip::tcp::acceptor ac{ioc, ep};
    ba::ip::tcp::socket soc{ioc};
    constexpr auto loop = [](auto loop, auto &ac, auto &soc) -> void {
        ac.async_accept(soc, [&](auto ec) {
            if (!ec)
                std::make_shared<connection>(std::move(soc))->start();
            loop(loop, ac, soc);
        });
    };
    loop(loop, ac, soc);
    ioc.run();
}
