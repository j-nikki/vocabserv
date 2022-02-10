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

#define KEEP_ALIVE_SECS 5

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

char *escape(const char *f, const char *l, char *d_f) noexcept
{
    using namespace std::string_view_literals;
    for (; f != l; ++f) {
        switch (const char c = *f) {
        case '<':
            d_f = sr::copy("&lt;"sv, d_f).out;
            break;
        case '>':
            d_f = sr::copy("&gt;"sv, d_f).out;
            break;
        case '&':
            d_f = sr::copy("&amp;"sv, d_f).out;
            break;
        default:
            *d_f++ = c;
        }
    }
    return d_f;
}
JUTIL_INLINE char *escape(std::string_view sv, char *d_f) noexcept
{
    return escape(sv.data(), sv.data() + sv.size(), d_f);
}

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
    case method::GET: {
        // No formatting time points on GCC ('22 Feb).
        // const auto date = sc::time_point_cast<sc::seconds>(sc::utc_clock::now());
        if (const auto [cont, type, hdr] = get_content(rq.strt.tgt); !cont.empty()) {
            rs.put("HTTP/1.1 200 OK\r\n"
                   "connection: keep-alive\r\n"
                   "content-type: {}; charset=UTF-8\r\n"
                   //"date: {:%a, %d %b %Y %T GMT}\r\n"
                   "content-length: {}\r\n"
                   "{}"
                   "keep-alive: timeout=" BOOST_STRINGIZE(KEEP_ALIVE_SECS) //
                   "\r\n\r\n"
                   "{}",
                   type, /*date,*/ cont.size(), hdr, cont);
        } else {
            char buf[500];
            const std::string_view bufv{buf, escape(rq.strt.tgt.sv().substr(0, 100), buf)};
            rs.put("HTTP/1.1 404 Not Found\r\n"
                   "content-type: text/html; charset=UTF-8\r\n"
                   "content-length: {}\r\n"
                   //"date: {:%a, %d %b %y %t gmt}\r\n"
                   "\r\n"
                   "{}{}{}",
                   nf1.size() + nf2.size() + bufv.size(), /*date,*/ nf1, bufv, nf2);
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

class connection : public std::enable_shared_from_this<connection>
{
  public:
    NO_DEF_CTORS(connection);
    connection(ba::ip::tcp::socket &&soc) : soc_{std::move(soc)} {}

    auto bound(auto f) { return std::bind_front(f, shared_from_this()); }
    void on_write(boost::system::error_code ec, std::size_t)
    {
        soc_.shutdown(ba::ip::tcp::socket::shutdown_send, ec);
        to_.cancel();
        DBGEXPR(printf("con#%d: write end\n", id_));
    }
    void on_timeout(const boost::system::error_code &ec)
    {
        if (!ec)
            DBGEXPR(printf("con#%d: timeout\n", id_)), soc_.close();
    }
    void on_read(const boost::system::error_code &ec, std::size_t n)
    {
        if (ec) {
            if (ec != ba::error::eof)
                g_log->error("{}: {}: {}\n", ec.category().name(), ec.value(),
                             ec.message().c_str());
            return;
        }
        parse_header(rq_.data(), rq_.data() + (n - 4), msg_);
        DBGEXPR(printf("vvv con#%d: received message with the header:\n", id_));
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
        DBGEXPR(printf("con#%d: accepted\n", id_));
        ba::async_read_until(soc_, ba::dynamic_buffer(rq_), "\r\n\r\n",
                             bound(&connection::on_read));
        to_.async_wait(bound(&connection::on_timeout));
    }

  private:
    ba::ip::tcp::socket soc_;
    ba::steady_timer to_{soc_.get_executor(), sc::seconds(KEEP_ALIVE_SECS)};
    message msg_;
    std::vector<char> rq_;
    buffer rs_;
    DBGSTMNT(const int id_ = ncon++;)
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
