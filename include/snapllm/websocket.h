/**
 * @file websocket.h
 * @brief WebSocket Protocol Implementation for SnapLLM Streaming
 *
 * Provides WebSocket support for real-time streaming inference.
 * Implements RFC 6455 WebSocket protocol for bidirectional communication.
 *
 * Usage:
 *   WebSocket connections are upgraded from HTTP at /ws/stream
 *   Messages are in ISON format for token efficiency
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <cstring>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define SOCKET_ERROR_CODE WSAGetLastError()
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define SOCKET_ERROR_CODE errno
#define closesocket close
#endif

namespace snapllm {

// ============================================================================
// WebSocket Frame Opcodes
// ============================================================================

enum class WSOpcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

// ============================================================================
// WebSocket Frame Structure
// ============================================================================

struct WSFrame {
    bool fin;
    WSOpcode opcode;
    bool masked;
    uint64_t payload_length;
    uint8_t mask_key[4];
    std::vector<uint8_t> payload;

    WSFrame() : fin(true), opcode(WSOpcode::Text), masked(false), payload_length(0) {
        memset(mask_key, 0, 4);
    }
};

// ============================================================================
// WebSocket Message Types (ISON Format)
// ============================================================================

namespace WSMessageType {
    constexpr const char* STREAM_START = "stream_start";
    constexpr const char* STREAM_TOKEN = "stream_token";
    constexpr const char* STREAM_END = "stream_end";
    constexpr const char* WS_ERROR = "error";  // Renamed from ERROR (conflicts with Windows macro)
    constexpr const char* WS_PING = "ping";
    constexpr const char* WS_PONG = "pong";
    constexpr const char* GENERATE_REQUEST = "generate";
    constexpr const char* CHAT_REQUEST = "chat";
    constexpr const char* MODEL_SWITCH = "model_switch";
}

// ============================================================================
// Base64 Encoding (for WebSocket handshake)
// ============================================================================

inline std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (data[i] << 16);
        if (i + 1 < len) n |= (data[i + 1] << 8);
        if (i + 2 < len) n |= data[i + 2];

        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? chars[n & 0x3F] : '=';
    }
    return result;
}

// ============================================================================
// SHA-1 Hash (for WebSocket handshake)
// ============================================================================

inline std::vector<uint8_t> sha1(const std::string& input) {
    // Simple SHA-1 implementation for WebSocket handshake
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    // Padding
    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t original_len = msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }

    // Append length in big-endian
    for (int i = 7; i >= 0; --i) {
        msg.push_back((original_len >> (i * 8)) & 0xFF);
    }

    // Process 512-bit blocks
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[80];

        // Break chunk into 16 32-bit big-endian words
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[offset + i*4] << 24) |
                   (msg[offset + i*4 + 1] << 16) |
                   (msg[offset + i*4 + 2] << 8) |
                   (msg[offset + i*4 + 3]);
        }

        // Extend to 80 words
        for (int i = 16; i < 80; i++) {
            uint32_t temp = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (temp << 1) | (temp >> 31);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    // Output in big-endian
    std::vector<uint8_t> result(20);
    for (int i = 0; i < 4; i++) {
        result[i] = (h0 >> (24 - i*8)) & 0xFF;
        result[i + 4] = (h1 >> (24 - i*8)) & 0xFF;
        result[i + 8] = (h2 >> (24 - i*8)) & 0xFF;
        result[i + 12] = (h3 >> (24 - i*8)) & 0xFF;
        result[i + 16] = (h4 >> (24 - i*8)) & 0xFF;
    }
    return result;
}

// ============================================================================
// WebSocket Connection Handler
// ============================================================================

class WebSocketConnection {
public:
    using MessageCallback = std::function<void(const std::string&, WSOpcode)>;
    using CloseCallback = std::function<void()>;

    WebSocketConnection(socket_t socket)
        : socket_(socket), connected_(true) {}

    ~WebSocketConnection() {
        close();
    }

    /**
     * @brief Complete WebSocket handshake
     * @param client_key The Sec-WebSocket-Key from client
     * @return true if handshake successful
     */
    bool completeHandshake(const std::string& client_key) {
        // WebSocket GUID as per RFC 6455
        const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string accept_key = client_key + guid;

        // SHA-1 hash and base64 encode
        auto hash = sha1(accept_key);
        std::string accept_value = base64_encode(hash.data(), hash.size());

        // Send handshake response
        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_value + "\r\n"
            "\r\n";

        return send_raw(response.data(), response.size());
    }

    /**
     * @brief Send text message
     */
    bool sendText(const std::string& message) {
        return sendFrame(WSOpcode::Text, message.data(), message.size());
    }

    /**
     * @brief Send binary message
     */
    bool sendBinary(const void* data, size_t len) {
        return sendFrame(WSOpcode::Binary, data, len);
    }

    /**
     * @brief Send ping
     */
    bool sendPing(const std::string& data = "") {
        return sendFrame(WSOpcode::Ping, data.data(), data.size());
    }

    /**
     * @brief Send pong
     */
    bool sendPong(const std::string& data = "") {
        return sendFrame(WSOpcode::Pong, data.data(), data.size());
    }

    /**
     * @brief Send close frame
     */
    bool sendClose(uint16_t code = 1000, const std::string& reason = "") {
        std::vector<uint8_t> payload;
        payload.push_back((code >> 8) & 0xFF);
        payload.push_back(code & 0xFF);
        payload.insert(payload.end(), reason.begin(), reason.end());
        return sendFrame(WSOpcode::Close, payload.data(), payload.size());
    }

    /**
     * @brief Read next frame
     */
    bool readFrame(WSFrame& frame) {
        if (!connected_) return false;

        uint8_t header[2];
        if (!recv_exact(header, 2)) return false;

        frame.fin = (header[0] & 0x80) != 0;
        frame.opcode = static_cast<WSOpcode>(header[0] & 0x0F);
        frame.masked = (header[1] & 0x80) != 0;
        frame.payload_length = header[1] & 0x7F;

        // Extended payload length
        if (frame.payload_length == 126) {
            uint8_t ext[2];
            if (!recv_exact(ext, 2)) return false;
            frame.payload_length = (ext[0] << 8) | ext[1];
        } else if (frame.payload_length == 127) {
            uint8_t ext[8];
            if (!recv_exact(ext, 8)) return false;
            frame.payload_length = 0;
            for (int i = 0; i < 8; i++) {
                frame.payload_length = (frame.payload_length << 8) | ext[i];
            }
        }

        // Mask key
        if (frame.masked) {
            if (!recv_exact(frame.mask_key, 4)) return false;
        }

        // Payload
        frame.payload.resize(frame.payload_length);
        if (frame.payload_length > 0) {
            if (!recv_exact(frame.payload.data(), frame.payload_length)) return false;

            // Unmask if needed
            if (frame.masked) {
                for (size_t i = 0; i < frame.payload_length; i++) {
                    frame.payload[i] ^= frame.mask_key[i % 4];
                }
            }
        }

        return true;
    }

    /**
     * @brief Close connection
     */
    void close() {
        if (connected_) {
            connected_ = false;
            closesocket(socket_);
        }
    }

    bool isConnected() const { return connected_; }
    socket_t getSocket() const { return socket_; }

    /**
     * @brief Send ISON-formatted streaming token
     */
    bool sendStreamToken(const std::string& token, int token_id = -1) {
        std::string msg = "msg.type stream_token\n";
        msg += "msg.token \"" + escapeString(token) + "\"\n";
        if (token_id >= 0) {
            msg += "msg.token_id " + std::to_string(token_id) + "\n";
        }
        return sendText(msg);
    }

    /**
     * @brief Send ISON-formatted stream start
     */
    bool sendStreamStart(const std::string& model, const std::string& request_id) {
        std::string msg = "msg.type stream_start\n";
        msg += "msg.model \"" + model + "\"\n";
        msg += "msg.request_id \"" + request_id + "\"\n";
        return sendText(msg);
    }

    /**
     * @brief Send ISON-formatted stream end
     */
    bool sendStreamEnd(int total_tokens, double generation_time_ms) {
        std::string msg = "msg.type stream_end\n";
        msg += "msg.total_tokens " + std::to_string(total_tokens) + "\n";
        msg += "msg.generation_time_ms " + std::to_string(generation_time_ms) + "\n";
        return sendText(msg);
    }

    /**
     * @brief Send ISON-formatted error
     */
    bool sendError(const std::string& error, const std::string& error_type = "server_error") {
        std::string msg = "msg.type error\n";
        msg += "msg.error \"" + escapeString(error) + "\"\n";
        msg += "msg.error_type \"" + error_type + "\"\n";
        return sendText(msg);
    }

private:
    socket_t socket_;
    bool connected_;

    bool send_raw(const void* data, size_t len) {
        const char* ptr = static_cast<const char*>(data);
        size_t sent = 0;
        while (sent < len) {
            int result = send(socket_, ptr + sent, static_cast<int>(len - sent), 0);
            if (result <= 0) return false;
            sent += result;
        }
        return true;
    }

    bool recv_exact(void* data, size_t len) {
        char* ptr = static_cast<char*>(data);
        size_t received = 0;
        while (received < len) {
            int result = recv(socket_, ptr + received, static_cast<int>(len - received), 0);
            if (result <= 0) return false;
            received += result;
        }
        return true;
    }

    bool sendFrame(WSOpcode opcode, const void* data, size_t len) {
        std::vector<uint8_t> frame;

        // First byte: FIN + opcode
        frame.push_back(0x80 | static_cast<uint8_t>(opcode));

        // Payload length (server doesn't mask)
        if (len < 126) {
            frame.push_back(static_cast<uint8_t>(len));
        } else if (len < 65536) {
            frame.push_back(126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((len >> (i * 8)) & 0xFF);
            }
        }

        // Payload
        const uint8_t* payload = static_cast<const uint8_t*>(data);
        frame.insert(frame.end(), payload, payload + len);

        return send_raw(frame.data(), frame.size());
    }

    std::string escapeString(const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '"') result += "\\\"";
            else if (c == '\\') result += "\\\\";
            else if (c == '\n') result += "\\n";
            else if (c == '\r') result += "\\r";
            else if (c == '\t') result += "\\t";
            else result += c;
        }
        return result;
    }
};

// ============================================================================
// WebSocket Upgrade Detection Helper
// ============================================================================

struct WebSocketUpgradeInfo {
    bool is_websocket_request;
    std::string websocket_key;
    std::string websocket_version;
    std::string websocket_protocol;

    WebSocketUpgradeInfo() : is_websocket_request(false) {}
};

/**
 * @brief Check if HTTP request is a WebSocket upgrade request
 */
inline WebSocketUpgradeInfo checkWebSocketUpgrade(
    const std::string& upgrade_header,
    const std::string& connection_header,
    const std::string& websocket_key_header,
    const std::string& websocket_version_header = "13",
    const std::string& websocket_protocol_header = ""
) {
    WebSocketUpgradeInfo info;

    // Check for upgrade headers
    if (upgrade_header.find("websocket") != std::string::npos ||
        upgrade_header.find("WebSocket") != std::string::npos) {
        if (connection_header.find("Upgrade") != std::string::npos ||
            connection_header.find("upgrade") != std::string::npos) {
            if (!websocket_key_header.empty()) {
                info.is_websocket_request = true;
                info.websocket_key = websocket_key_header;
                info.websocket_version = websocket_version_header;
                info.websocket_protocol = websocket_protocol_header;
            }
        }
    }

    return info;
}

} // namespace snapllm
