#include "../include/websockets.hpp"
#include "../include/match.hpp"
#include "../include/epoll_loop.hpp"
#include <errno.h>
#include <fcntl.h>
#include <cstring>
#include <poll.h>

static ssize_t recv_retry(int fd, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t n = recv(fd, static_cast<char *>(buf) + total, len - total, 0);
        if (n > 0)
        {
            total += static_cast<size_t>(n);
            continue;
        }
        if (n == 0)
            return 0;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, -1) <= 0)
                return -1;
            continue;
        }
        return -1;
    }
    return static_cast<ssize_t>(total);
}

// helper for encoding base64
std::string base64_encode(const unsigned char *data, size_t len)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);

    BUF_MEM *memPtr;
    BIO_get_mem_ptr(b64, &memPtr);

    std::string out(memPtr->data, memPtr->length);

    BIO_free_all(b64);
    return out;
}

bool is_websocket_upgrade(const std::string &raw, std::string &secKeyOut)
{
    std::istringstream iss(raw);
    std::string line;

    bool hasUpgrade = false;
    bool hasConnection = false;
    std::string secKey;

    // skip request line, start from headers
    std::getline(iss, line);

    while (std::getline(iss, line))
    {
        if (line == "\r" || line.empty())
            break;

        auto pos = line.find(':');
        if (pos == std::string::npos)
            continue;
        std::string name = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        // trim
        auto trim = [](std::string &s)
        {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                ++i;
            s.erase(0, i);
        };
        trim(name);
        trim(value);

        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        if (lowerName == "upgrade" && value.find("websocket") != std::string::npos)
        {
            hasUpgrade = true;
        }
        if (lowerName == "connection" && value.find("Upgrade") != std::string::npos)
        {
            hasConnection = true;
        }
        if (lowerName == "sec-websocket-key")
        {
            secKey = value;
        }
    }

    if (hasUpgrade && hasConnection && !secKey.empty())
    {
        secKeyOut = secKey;
        return true;
    }
    return false;
}

std::string compute_websocket_accept(const std::string &secKey)
{
    static const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string toHash = secKey + GUID;

    unsigned char sha1[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(toHash.data()), toHash.size(), sha1);

    return base64_encode(sha1, SHA_DIGEST_LENGTH);
}

bool recv_ws_frame(int fd, std::string &outPayload)
{
    uint8_t header[2];
    ssize_t n = recv_retry(fd, header, 2);
    if (n <= 0)
        return false;

    [[maybe_unused]] bool fin = (header[0] & 0x80) != 0;
    uint8_t opcode = header[0] & 0x0F;
    bool mask = (header[1] & 0x80) != 0;
    uint64_t len = header[1] & 0x7F;

    if (!mask)
    {
        // Client frames MUST be masked
        return false;
    }

    if (len == 126)
    {
        uint8_t ext[2];
        n = recv_retry(fd, ext, 2);
        if (n <= 0)
            return false;
        len = (ext[0] << 8) | ext[1];
    }
    else if (len == 127)
    {
        // skipping 64-bit payloads for brevity
        return false;
    }

    uint8_t maskKey[4];
    n = recv_retry(fd, maskKey, 4);
    if (n <= 0)
        return false;

    std::string payload(len, '\0');
    n = recv_retry(fd, payload.data(), len);
    if (n <= 0)
        return false;

    for (uint64_t i = 0; i < len; ++i)
    {
        payload[i] ^= maskKey[i % 4];
    }

    if (opcode == 0x8)
    {
        // close frame
        return false;
    }
    if (opcode == 0x1)
    {
        // text frame
        outPayload = payload;
        return true;
    }

    // ignore other opcodes
    return true;
}

void send_ws_text(int fd, const std::string &msg)
{
    uint8_t header[10];
    size_t len = msg.size();
    size_t headerLen = 0;

    header[0] = 0x81; // FIN=1, opcode=1 (text)

    if (len <= 125)
    {
        header[1] = static_cast<uint8_t>(len);
        headerLen = 2;
    }
    else if (len < 65536)
    {
        header[1] = 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        headerLen = 4;
    }
    else
    {
        // large frames not supported in this minimal implementation
        return;
    }

    std::string frame;
    frame.resize(headerLen + msg.size());
    memcpy(&frame[0], header, headerLen);
    memcpy(&frame[headerLen], msg.data(), msg.size());

    // attempt non-blocking send
    ssize_t s = send(fd, frame.data(), frame.size(), 0);
    if (s == (ssize_t)frame.size()) return; // sent all

    if (s >= 0) {
        // partial send: buffer remainder
        size_t offset = (size_t)s;
        auto& ctx = get_match_context();
        std::lock_guard<std::mutex> lock(ctx.wsClientsMutex);
        for (auto &c : ctx.wsClients) {
            if (c.fd == fd) {
                c.outBuf.append(frame.data() + offset, frame.size() - offset);
                enable_epoll_write(fd);
                return;
            }
        }
        return;
    }

    if (s < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // socket would block: buffer entire frame and enable epoll write
        auto& ctx = get_match_context();
        std::lock_guard<std::mutex> lock(ctx.wsClientsMutex);
        for (auto &c : ctx.wsClients) {
            if (c.fd == fd) {
                c.outBuf.append(frame);
                enable_epoll_write(fd);
                return;
            }
        }
        return;
    }

    // other errors: ignore for now
}