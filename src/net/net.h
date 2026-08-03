#pragma once
// LAN 联机网络层（P8）：Winsock2 TCP 非阻塞，[len:u16][type:u8][payload] 消息帧。
// lockstep 只需可靠有序的字节流：命令帧/握手/校验和全走 TCP，LAN 延迟下无需 UDP。
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <vector>

class NetLink {
public:
    static constexpr size_t MAX_BODY_BYTES = 60000;
    static constexpr size_t MAX_BUFFER_BYTES = 1u << 20;
    static constexpr size_t MAX_INBOX_MESSAGES = 256;
    // 消息类型
    enum MsgType : uint8_t {
        MsgHello = 1,   // C→S：version u32, country u8, color u8
        MsgWelcome,     // S→C：seed u64, mapSize u8, mapType u8, money i32, crates u8, hostCountry u8, hostColor u8
        MsgStart,       // S→C：开始对局（双方同步 newGame）
        MsgCmdFrame,    // 双向：tick u32, ncmds u8, cmds[]（World::Cmd 序列化）
        MsgChecksum,    // 双向：tick u32, sum u32（反不同步）
        MsgBye,         // 双向：断线通知
    };
    struct Msg {
        uint8_t type = 0;
        std::vector<uint8_t> body; // payload（不含帧头）
    };

    NetLink() = default;
    ~NetLink() { close(); }
    NetLink(const NetLink&) = delete;
    NetLink& operator=(const NetLink&) = delete;

    bool host(int port);                 // 监听并等待一个连接（非阻塞，accepted() 轮询）
    bool connectTo(const char* ip, int port); // 非阻塞连接，connected() 轮询结果
    bool accepted() const { return state == State::Linked; }
    bool connected() const { return state == State::Linked; }
    bool failed() const { return state == State::Failed; }
    bool listening() const { return state == State::Listening || state == State::Connecting; }
    void close();

    void poll();                         // 收取数据并解包到 inbox（每帧调用）
    bool send(uint8_t type, const std::vector<uint8_t>& body);
    bool send(uint8_t type) { return send(type, {}); }
    std::deque<Msg> inbox;               // 已解包消息（Game 消费后 pop_front）

    // 序列化助手。线协议当前固定为小端；memcpy 避免未对齐/别名 UB。
    struct Writer {
        std::vector<uint8_t> b;
        template <class T> void w(const T& v) {
            static_assert(std::is_trivially_copyable_v<T>);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            b.insert(b.end(), p, p + sizeof(T));
        }
    };
    struct Reader {
        const uint8_t* p = nullptr;
        size_t n = 0;
        bool ok = true;
        explicit Reader(const std::vector<uint8_t>& b) : p(b.data()), n(b.size()) {}
        template <class T> T r() {
            static_assert(std::is_trivially_copyable_v<T>);
            T v{};
            if (n < sizeof(T)) { ok = false; return v; }
            std::memcpy(&v, p, sizeof(T));
            p += sizeof(T); n -= sizeof(T);
            return v;
        }
        bool done() const { return ok && n == 0; }
    };

private:
    enum class State { Closed, Listening, Connecting, Linked, Failed };
    State state = State::Closed;
    uintptr_t sock = ~0ull;   // SOCKET
    uintptr_t listenSock = ~0ull;
    std::vector<uint8_t> rxBuf; // 未解包字节流
    std::string outBuf;         // 待发送字节流（非阻塞写补偿）

    void acceptClient();
    void flushOut();
    void fail();
};
