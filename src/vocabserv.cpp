#include "vocabserv.h"

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdio.h>

#include "server.h"

std::shared_ptr<spdlog::logger> g_log;
vocab g_vocab;

#ifdef WIN32
#define argfmt "%S"
#define READPORT(X, RP) (swscanf(X, L"%hu", &(RP)) == 1)
int wmain(int argc, wchar_t **argv)
#else
#define argfmt "%s"
#define READPORT(X, RP) (sscanf(X, "%hu", &(RP)) == 1)
int main(int argc, char **argv)
#endif
{
    try {
        if (argc < 2) {
            fprintf(stderr, "usage: " argfmt "<vocab-path> [<port-num>] [<log-prefix>]\n", argv[0]);
            return 1;
        }

        if (!g_vocab.init(argv[1])) {
            fprintf(stderr, "couldn't open vocab file \"" argfmt "\"\n", argv[1]);
            return 1;
        }

        unsigned short port = 80;
        if (argc >= 3 && !READPORT(argv[2], port)) {
            fprintf(stderr, "couldn't read port as int (\"" argfmt "\")", argv[2]);
            return 1;
        }

        // will report errors through exception
        g_log = argc == 4 ? spdlog::daily_logger_mt("l", argv[3], 3) : spdlog::stderr_color_mt("l");

        DBGEXPR(printf("server will run on 0.0.0.0:%hu...\n", port));
        run_server({boost::asio::ip::address_v4{0}, static_cast<boost::asio::ip::port_type>(port)});

        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, argfmt ": exception occurred: %s\n", argv[0], e.what());
        return 1;
    }
}

bool vocab::init(const std::filesystem::path &path)
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
