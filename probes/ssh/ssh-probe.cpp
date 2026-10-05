// Portable installed-libssh2 consumer, also hosted by the Xbox SDL probe.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
using SOCKET = int;
constexpr int INVALID_SOCKET = -1;
#endif
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

static void require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}

static std::string read_file(const char *path)
{
    std::ifstream input{path, std::ios::binary};
    require(input.good(), "cannot read runtime input file");
    return {std::istreambuf_iterator<char>{input}, {}};
}

int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7) {
        std::puts("Usage: ssh-probe IPv4 PORT KNOWN_HOSTS PRIVATE_KEY USER [--file-auth]");
        return 1;
    }
    SOCKET socket = INVALID_SOCKET;
    LIBSSH2_SESSION *session = nullptr;
    LIBSSH2_KNOWNHOSTS *hosts = nullptr;
    LIBSSH2_CHANNEL *channel = nullptr;
    LIBSSH2_SFTP *sftp = nullptr;
    LIBSSH2_SFTP_HANDLE *file = nullptr;
    int result = 1;
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
#endif
    bool initialized = false;
    try {
        require(libssh2_init(0) == 0, "libssh2_init failed");
        initialized = true;
        const int port = std::stoi(argv[2]);
        require(port > 0 && port <= 65535, "invalid port");
        const std::string known_hosts = read_file(argv[3]);
        const std::string private_key = read_file(argv[4]);
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        require(socket != INVALID_SOCKET, "socket failed");
#ifdef _WIN32
        DWORD timeout = 15000;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
#else
        timeval timeout{15, 0};
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(port));
        require(inet_pton(AF_INET, argv[1], &address.sin_addr) == 1, "invalid IPv4 address");
        require(connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "connect failed");
        session = libssh2_session_init();
        require(session != nullptr, "session init failed");
        libssh2_session_set_timeout(session, 15000);
        require(libssh2_session_handshake(session, socket) == 0, "SSH handshake failed");
        hosts = libssh2_knownhost_init(session);
        require(hosts != nullptr, "knownhost init failed");
        size_t start = 0;
        while (start < known_hosts.size()) {
            const auto end = known_hosts.find('\n', start);
            const auto length = (end == std::string::npos ? known_hosts.size() : end) - start;
            if (length) require(libssh2_knownhost_readline(hosts, known_hosts.data() + start,
                length + (end != std::string::npos), LIBSSH2_KNOWNHOST_FILE_OPENSSH) == 0, "invalid known_hosts");
            start += length + 1;
        }
        size_t key_length = 0;
        int key_type = 0;
        const char *key = libssh2_session_hostkey(session, &key_length, &key_type);
        require(key != nullptr, "missing server host key");
        // The received wire key includes its algorithm. OpenSSH known_hosts
        // parsing supplies the expected algorithm as well as key bytes.
        libssh2_knownhost *matched = nullptr;
        const int trust = libssh2_knownhost_checkp(hosts, argv[1], port, key, key_length,
            LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, &matched);
        if (trust != LIBSSH2_KNOWNHOST_CHECK_MATCH) {
            std::puts("REJECTED host key before authentication");
            result = 2;
        } else {
            std::puts("PASS pinned host key");
            const std::string user{argv[5]};
            int auth;
            if (argc == 7) {
                require(std::string{argv[6]} == "--file-auth", "unknown option");
                auth = libssh2_userauth_publickey_fromfile(session, user.c_str(), nullptr, argv[4], nullptr);
            } else {
                auth = libssh2_userauth_publickey_frommemory(session, user.data(), user.size(),
                    nullptr, 0, private_key.data(), private_key.size(), nullptr);
            }
            require(auth == 0, "public-key authentication failed");
            std::puts(argc == 7 ? "PASS file public-key authentication" : "PASS memory public-key authentication");
            channel = libssh2_channel_open_session(session);
            require(channel != nullptr, "channel open failed");
            require(libssh2_channel_exec(channel,
                "printf 'Xbox SSH online\\n'; printf 'stderr-ok\\n' >&2; exit 7") == 0, "exec failed");
            std::string output, error;
            char buffer[1024];
            for (int stream = 0; stream < 2; ++stream) {
                for (;;) {
                    const auto count = libssh2_channel_read_ex(channel, stream, buffer, sizeof(buffer));
                    require(count >= 0, "channel read failed");
                    if (count == 0) break;
                    (stream == 0 ? output : error).append(buffer, static_cast<size_t>(count));
                }
            }
            require(libssh2_channel_close(channel) == 0, "channel close failed");
            require(libssh2_channel_wait_closed(channel) == 0, "channel wait closed failed");
            require(output == "Xbox SSH online\n" && error == "stderr-ok\n", "stdout/stderr mismatch");
            require(libssh2_channel_get_exit_status(channel) == 7, "exit status mismatch");
            libssh2_channel_free(channel);
            channel = nullptr;
            std::puts("PASS exec stdout/stderr and exit status 7");
            sftp = libssh2_sftp_init(session);
            require(sftp != nullptr, "SFTP init failed");
            file = libssh2_sftp_open(sftp, "probe.bin", LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0600);
            require(file != nullptr, "SFTP write open failed");
            const std::string payload{"Xbox\0SFTP\xff", 10};
            size_t offset = 0;
            while (offset < payload.size()) {
                const auto count = libssh2_sftp_write(file, payload.data() + offset, payload.size() - offset);
                require(count > 0, "SFTP write failed");
                offset += static_cast<size_t>(count);
            }
            require(libssh2_sftp_close(file) == 0, "SFTP write close failed");
            file = nullptr;
            file = libssh2_sftp_open(sftp, "probe.bin", LIBSSH2_FXF_READ, 0);
            require(file != nullptr, "SFTP read open failed");
            std::string received;
            for (;;) {
                const auto count = libssh2_sftp_read(file, buffer, sizeof(buffer));
                require(count >= 0, "SFTP read failed");
                if (count == 0) break;
                received.append(buffer, static_cast<size_t>(count));
            }
            require(received == payload, "SFTP binary round trip mismatch");
            require(libssh2_sftp_close(file) == 0, "SFTP read close failed");
            file = nullptr;
            require(libssh2_sftp_unlink(sftp, "probe.bin") == 0, "SFTP unlink failed");
            std::puts("PASS SFTP binary upload/download/unlink");
            result = 0;
        }
    } catch (const std::exception &error) {
        std::printf("FAIL %s\n", error.what());
        if (session) {
            char *message = nullptr;
            libssh2_session_last_error(session, &message, nullptr, 0);
            if (message) std::printf("libssh2: %s\n", message);
        }
    }
    if (file) libssh2_sftp_close(file);
    if (sftp) libssh2_sftp_shutdown(sftp);
    if (channel) libssh2_channel_free(channel);
    if (hosts) libssh2_knownhost_free(hosts);
    if (session) {
        libssh2_session_disconnect(session, "probe finished");
        libssh2_session_free(session);
    }
    if (socket != INVALID_SOCKET) closesocket(socket);
    if (initialized) libssh2_exit();
#ifdef _WIN32
    WSACleanup();
#endif
    std::fflush(stdout);
    return result;
}
