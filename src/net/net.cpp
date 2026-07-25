#include "net/net.h"
// Winsock2 实现：非阻塞 TCP + 消息帧解包/组包
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace {
struct WsaInit {
    int n = 0;
    WsaInit() { WSADATA d; if (WSAStartup(MAKEWORD(2, 2), &d) == 0) n = 1; }
    ~WsaInit() { if (n) WSACleanup(); }
};
WsaInit g_wsa; // 进程级 WSA 生命周期
} // namespace

static void setNonBlock(SOCKET s) {
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    // 关闭 Nagle：lockstep 小帧需要立即送达
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

bool NetLink::host(int port) {
    close();
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return false;
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, 1) != 0) {
        closesocket(ls);
        return false;
    }
    setNonBlock(ls);
    listenSock = (uintptr_t)ls;
    state = State::Listening;
    return true;
}

bool NetLink::connectTo(const char* ip, int port) {
    close();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { state = State::Failed; return false; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    if (InetPtonA(AF_INET, ip, &addr.sin_addr) != 1) { closesocket(s); state = State::Failed; return false; }
    setNonBlock(s);
    int r = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (r != 0 && WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(s); state = State::Failed; return false; }
    sock = (uintptr_t)s;
    state = State::Connecting;
    return true;
}

void NetLink::acceptClient() {
    if (state != State::Listening) return;
    SOCKET c = accept((SOCKET)listenSock, nullptr, nullptr);
    if (c == INVALID_SOCKET) return;
    setNonBlock(c);
    sock = (uintptr_t)c;
    closesocket((SOCKET)listenSock);
    listenSock = ~0ull;
    state = State::Linked;
}

void NetLink::close() {
    if (sock != ~0ull) { closesocket((SOCKET)sock); sock = ~0ull; }
    if (listenSock != ~0ull) { closesocket((SOCKET)listenSock); listenSock = ~0ull; }
    state = State::Closed;
    rxBuf.clear();
    outBuf.clear();
    inbox.clear();
}

void NetLink::fail() {
    if (sock != ~0ull) { closesocket((SOCKET)sock); sock = ~0ull; }
    if (listenSock != ~0ull) { closesocket((SOCKET)listenSock); listenSock = ~0ull; }
    state = State::Failed;
}

void NetLink::flushOut() {
    while (!outBuf.empty()) {
        int n = ::send((SOCKET)sock, outBuf.data(), (int)outBuf.size(), 0);
        if (n > 0) { outBuf.erase(0, n); continue; }
        if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK) { fail(); return; }
        break; // EWOULDBLOCK：下帧再发
    }
}

bool NetLink::send(uint8_t type, const std::vector<uint8_t>& body) {
    if (state != State::Linked) return false;
    if (body.size() > 60000) return false; // 单帧上限
    uint16_t len = (uint16_t)(body.size() + 1);
    outBuf.append((const char*)&len, 2);
    outBuf.push_back((char)type);
    if (!body.empty()) outBuf.append((const char*)body.data(), body.size());
    flushOut();
    return state == State::Linked;
}

void NetLink::poll() {
    if (state == State::Listening) { acceptClient(); return; }
    if (state == State::Connecting) {
        // 非阻塞 connect 完成检测：可写即成功（SO_ERROR 复核）
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET((SOCKET)sock, &wf);
        timeval tv{0, 0};
        if (select(0, nullptr, &wf, nullptr, &tv) > 0) {
            int err = 0, len = sizeof(err);
            getsockopt((SOCKET)sock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            if (err == 0) state = State::Linked;
            else fail();
        }
        return;
    }
    if (state != State::Linked) return;
    flushOut();
    if (state != State::Linked) return;
    // 收数据
    uint8_t buf[16384];
    for (;;) {
        int n = recv((SOCKET)sock, (char*)buf, sizeof(buf), 0);
        if (n > 0) { rxBuf.insert(rxBuf.end(), buf, buf + n); continue; }
        if (n == 0) { fail(); return; } // 对端关闭
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK) { fail(); return; }
        break;
    }
    // 解包完整帧 [len:u16][type:u8][payload]
    for (;;) {
        if (rxBuf.size() < 3) break;
        uint16_t len = *(const uint16_t*)rxBuf.data();
        if (len < 1 || rxBuf.size() < (size_t)len + 2) break;
        Msg m;
        m.type = rxBuf[2];
        m.body.assign(rxBuf.begin() + 3, rxBuf.begin() + 2 + len);
        inbox.push_back(std::move(m));
        rxBuf.erase(rxBuf.begin(), rxBuf.begin() + 2 + len);
    }
}
