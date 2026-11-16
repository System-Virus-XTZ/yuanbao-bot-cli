// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 System-Virus-XTZ
//
// 元宝 Bot CLI —— 腾讯元宝开放平台 Bot 纯命令行工具（纯 C++17，无 Web 前端）
// Licensed under the GNU Affero General Public License v3.0 (the "License");
// you may not use this file except in compliance with the License.
// See the LICENSE file in the project root for the full license text.

#include <string>
#include <vector>
#include <deque>
#include <utility>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#ifndef YUANBAO_NO_OPENSSL
  #include <openssl/ssl.h>
#endif

// ═══════════════════════════════════════════════
//  跨平台兼容层
// ═══════════════════════════════════════════════
#if defined(_WIN32) || defined(_WIN64)
  #define YUANBAO_PLATFORM_WINDOWS 1
  #define WIN32_LEAN_AND_MEAN
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <io.h>
  #ifdef REPEAT
    #undef REPEAT
  #endif
  #ifdef DEFAULT
    #undef DEFAULT
  #endif
  #ifdef ALTERNATE
    #undef ALTERNATE
  #endif
  #ifdef RANDOM
    #undef RANDOM
  #endif
  #ifdef DELETE
    #undef DELETE
  #endif
  #ifdef GetMessage
    #undef GetMessage
  #endif
  #ifdef SendMessage
    #undef SendMessage
  #endif
  #ifdef GetObject
    #undef GetObject
  #endif
  #ifdef ERROR
    #undef ERROR
  #endif
  #ifdef RGB
    #undef RGB
  #endif
  #ifdef TRANSPARENT
    #undef TRANSPARENT
  #endif

  #ifndef ssize_t
    typedef SSIZE_T ssize_t;
  #endif

  #ifndef close_socket
    #define close_socket(s) ::closesocket(s)
  #endif
  #ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0
  #endif

  #include <direct.h>
  #define mkdir(path, mode) _mkdir(path)
  #define unlink _unlink
  #define localtime_r(tp, tm) localtime_s(tm, tp)

  struct yb_dirent { char d_name[260]; };
  typedef struct { HANDLE handle; WIN32_FIND_DATAA findData; yb_dirent ent; int first; } yb_DIR;

  inline yb_DIR* yb_opendir(const char* path) {
      std::string pattern(path);
      if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/')
          pattern += "\\*";
      else pattern += "*";
      yb_DIR* d = new yb_DIR();
      d->handle = FindFirstFileA(pattern.c_str(), &d->findData);
      if (d->handle == INVALID_HANDLE_VALUE) { delete d; return nullptr; }
      d->first = 1;
      return d;
  }
  inline yb_dirent* yb_readdir(yb_DIR* d) {
      if (!d) return nullptr;
      if (d->first) { d->first = 0; }
      else { if (!FindNextFileA(d->handle, &d->findData)) return nullptr; }
      strncpy(d->ent.d_name, d->findData.cFileName, sizeof(d->ent.d_name) - 1);
      d->ent.d_name[sizeof(d->ent.d_name) - 1] = '\0';
      return &d->ent;
  }
  inline void yb_closedir(yb_DIR* d) { if (d) { FindClose(d->handle); delete d; } }
  #define opendir yb_opendir
  #define readdir yb_readdir
  #define closedir yb_closedir
  #define dirent yb_dirent
  #define DIR yb_DIR

  #ifndef MAP_FAILED
    #define MAP_FAILED ((void*)-1)
  #endif

#elif defined(__linux__) || defined(__ANDROID__)
  #define YUANBAO_PLATFORM_LINUX 1
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <sys/time.h>
  #include <sys/select.h>
  #include <signal.h>
  #include <netdb.h>
  #include <dirent.h>
  #include <errno.h>
  #define close_socket(s) ::close(s)

#elif defined(__APPLE__) || defined(__MACH__)
  #define YUANBAO_PLATFORM_MACOS 1
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <sys/time.h>
  #include <sys/select.h>
  #include <signal.h>
  #include <netdb.h>
  #include <dirent.h>
  #include <errno.h>
  #define close_socket(s) ::close(s)

#else
  #error "Unsupported platform"
#endif

// 安全数字解析：std::stoi/stoul 遇到非数字会抛异常（未捕获会直接 terminate），
// 这里统一吞掉异常并返回兜底值 —— 网络响应/配置文件/端口等都可能是脏数据
static unsigned long long safe_stoull(const std::string& s, int base = 10) {
    try {
        size_t pos = 0;
        return std::stoull(s, &pos, base);
    } catch (...) {
        return 0;
    }
}

static int safe_stoi(const std::string& s, int def = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

// ═══════════════════════════════════════════════
//  跨平台网络工具
// ═══════════════════════════════════════════════
namespace yb_platform {

inline bool net_init() {
#ifdef YUANBAO_PLATFORM_WINDOWS
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

inline void net_cleanup() {
#ifdef YUANBAO_PLATFORM_WINDOWS
    WSACleanup();
#endif
}

inline bool set_recv_timeout(int fd, int timeout_ms) {
#ifdef YUANBAO_PLATFORM_WINDOWS
    DWORD tv = (DWORD)timeout_ms;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

inline bool set_send_timeout(int fd, int timeout_ms) {
#ifdef YUANBAO_PLATFORM_WINDOWS
    DWORD tv = (DWORD)timeout_ms;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

inline uint64_t now_ms() {
#ifdef YUANBAO_PLATFORM_WINDOWS
    static LARGE_INTEGER freq;   // 静态变量默认零初始化
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000 / freq.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

} // namespace yb_platform

#ifdef YUANBAO_PLATFORM_WINDOWS
  #define YB_SEND(fd, buf, len, flags) ::send(fd, (const char*)(buf), (int)(len), flags)
  #define YB_RECV(fd, buf, len, flags) ::recv(fd, (char*)(buf), (int)(len), flags)
#else
  #define YB_SEND(fd, buf, len, flags) ::send(fd, buf, len, flags | MSG_NOSIGNAL)
  #define YB_RECV(fd, buf, len, flags) ::recv(fd, buf, len, flags)
#endif
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <memory>
#include <variant>
#include <deque>
#include <set>
#include <algorithm>
#include <cctype>
#include <cstdlib>

// ═══════════════════════════════════════════════
//  基础类型别名
// ═══════════════════════════════════════════════
using Bytes = std::vector<uint8_t>;

inline void bytes_append(Bytes& dst, const Bytes& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}
inline Bytes bytes_concat(const Bytes& a, const Bytes& b) {
    Bytes r = a; r.insert(r.end(), b.begin(), b.end()); return r;
}

// ═══════════════════════════════════════════════
//  工具函数
// ═══════════════════════════════════════════════
namespace util {

inline std::string random_id(int len = 16) {
    static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 61);
    std::string r;
    for (int i = 0; i < len; i++) r += chars[dis(gen)];
    return r;
}
// Python uuid.uuid4().hex → 32 字符小写 hex
inline std::string random_hex_id() {
    static const char hx[] = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::string r;
    for (int i = 0; i < 32; i++) r += hx[dis(gen)];
    return r;
}

// 解析图片宽高（PNG/JPEG/GIF/BMP），解析失败返回 false
inline bool parse_image_dimensions(const std::string& data, int& w, int& h) {
    w = h = 0;
    if (data.size() < 16) return false;
    // PNG: 8字节签名 + IHDR(宽高在 16-24 字节，大端)
    if (data.size() >= 24 &&
        (uint8_t)data[0] == 0x89 && (uint8_t)data[1] == 0x50 && (uint8_t)data[2] == 0x4E && (uint8_t)data[3] == 0x47) {
        w = ((uint8_t)data[16] << 24) | ((uint8_t)data[17] << 16) | ((uint8_t)data[18] << 8) | (uint8_t)data[19];
        h = ((uint8_t)data[20] << 24) | ((uint8_t)data[21] << 16) | ((uint8_t)data[22] << 8) | (uint8_t)data[23];
        return w > 0 && h > 0;
    }
    // JPEG: FF D8 FF ... 扫描 SOFn 段（C0-CF 但排除 C4/C8/CC）
    if ((uint8_t)data[0] == 0xFF && (uint8_t)data[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < data.size()) {
            if ((uint8_t)data[i] != 0xFF) { i++; continue; }
            unsigned char marker = (uint8_t)data[i + 1];
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                h = ((uint8_t)data[i + 5] << 8) | (uint8_t)data[i + 6];
                w = ((uint8_t)data[i + 7] << 8) | (uint8_t)data[i + 8];
                return w > 0 && h > 0;
            }
            i += 2;
            size_t seg_len = ((uint8_t)data[i] << 8) | (uint8_t)data[i + 1];
            i += seg_len;
        }
        return false;
    }
    // GIF: GIF87a/GIF89a，宽高在 6-10 字节（小端）
    if (data.size() >= 10 &&
        data.compare(0, 3, "GIF") == 0) {
        w = (uint8_t)data[6] | ((uint8_t)data[7] << 8);
        h = (uint8_t)data[8] | ((uint8_t)data[9] << 8);
        return w > 0 && h > 0;
    }
    // BMP: "BM"，宽高在 18-26 字节（小端 int32）
    if (data.size() >= 26 && data.compare(0, 2, "BM") == 0) {
        w = (uint8_t)data[18] | ((uint8_t)data[19] << 8) | ((uint8_t)data[20] << 16) | ((uint8_t)data[21] << 24);
        h = (uint8_t)data[22] | ((uint8_t)data[23] << 8) | ((uint8_t)data[24] << 16) | ((uint8_t)data[25] << 24);
        if (h < 0) h = -h;
        return w > 0 && h > 0;
    }
    return false;
}

// URL 解码：%XX → 字符，+ → 空格
inline std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hexv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hexv(s[i + 1]), l = hexv(s[i + 2]);
            if (h >= 0 && l >= 0) {
                out += (char)((h << 4) | l);
                i += 2;
                continue;
            }
            out += s[i];
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

// ← 补：URL 百分号编码（非 ASCII / 保留字符 → %XX），用于构造带中文参数/路径的 URL
inline std::string url_encode(const std::string& s) {
    static const char* hexdig = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hexdig[(c >> 4) & 0xF];
            out += hexdig[c & 0xF];
        }
    }
    return out;
}

// ← 补：UTF-8 与宽字符互转（Windows 下中文文件名必须用宽字符 API 读写）
inline std::wstring utf8_to_wide(const std::string& s) {
#ifdef YUANBAO_PLATFORM_WINDOWS
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
#else
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0; int take = 0;
        if (c < 0x80) { cp = c; take = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; take = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; take = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; take = 4; }
        else { w += (wchar_t)s[i]; i++; continue; }
        if (i + take > s.size()) { i++; continue; }
        for (int k = 1; k < take; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        if (cp > 0xFFFF) { cp -= 0x10000; w += (wchar_t)(0xD800 + (cp >> 10)); w += (wchar_t)(0xDC00 + (cp & 0x3FF)); }
        else w += (wchar_t)cp;
        i += take;
    }
    return w;
#endif
}

// UTF-8 → 系统 ANSI 代码页（Windows 下把命令行交给 cmd.exe 时必须转，
// 否则命令里的中文参数会被按 GBK 误解码，导致乱码）
inline std::string utf8_to_acp(const std::string& s) {
#ifdef YUANBAO_PLATFORM_WINDOWS
    if (s.empty()) return std::string();
    std::wstring w = utf8_to_wide(s);
    if (w.empty()) return s;
    int n = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return s;
    std::string out(n, 0);
    WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
#else
    return s;
#endif
}

inline std::string wide_to_utf8(const std::wstring& w) {
#ifdef YUANBAO_PLATFORM_WINDOWS
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
#else
    std::string s;
    for (wchar_t ch : w) {
        uint32_t cp = (uint32_t)ch;
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            cp = 0x10000 + ((uint32_t)(ch - 0xD800) << 10);
        } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
            cp += (uint32_t)(ch - 0xDC00);
            // 写入合成后的码点
        }
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }
    return s;
#endif
}

// ← 补：按 UTF-8 路径读取二进制文件（Windows 走宽字符 API，支持中文文件名）
inline bool read_file_utf8(const std::string& path_utf8, std::string& out) {
    std::ifstream f(std::filesystem::path(utf8_to_wide(path_utf8)), std::ios::binary);
    if (!f.good()) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

inline uint64_t now_ms() {
    return yb_platform::now_ms();
}

inline std::string time_iso() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return std::string(buf);
}

inline std::string current_date() {
    time_t t = time(nullptr);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d", &tm_buf);
    return std::string(buf);
}

inline std::string hex_encode(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return oss.str();
}

inline std::string hex_decode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        int v;
        std::stringstream ss(s.substr(i, 2));
        ss >> std::hex >> v;
        r += (char)v;
    }
    return r;
}

struct MultipartPart {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string data;
};

/** 解析 multipart/form-data（与前端 FormData 上传一致） */
inline bool parse_multipart(const std::string& content_type, const std::string& body,
                            std::vector<MultipartPart>& parts) {
    size_t bpos = content_type.find("boundary=");
    if (bpos == std::string::npos) return false;
    std::string boundary = "--" + content_type.substr(bpos + 9);
    while (!boundary.empty() && (boundary.back() == '\r' || boundary.back() == '\n' || boundary.back() == '"' || boundary.back() == ' '))
        boundary.pop_back();

    size_t pos = body.find(boundary);
    while (pos != std::string::npos) {
        size_t part_start = pos + boundary.size();
        if (part_start + 1 < body.size() && body[part_start] == '-' && body[part_start + 1] == '-')
            break; // 结尾 --boundary--
        if (part_start + 1 < body.size() && body[part_start] == '\r' && body[part_start + 1] == '\n')
            part_start += 2;
        size_t header_end = body.find("\r\n\r\n", part_start);
        if (header_end == std::string::npos) break;
        std::string headers = body.substr(part_start, header_end - part_start);
        size_t data_start = header_end + 4;
        size_t next_boundary = body.find(boundary, data_start);
        if (next_boundary == std::string::npos) break;

        std::string data = body.substr(data_start, next_boundary - data_start);
        if (!data.empty() && data.size() >= 2 && data.substr(data.size() - 2) == "\r\n")
            data.erase(data.size() - 2);

        MultipartPart part;
        part.content_type = "application/octet-stream";
        size_t ct = headers.find("Content-Type:");
        if (ct != std::string::npos) {
            size_t cs = ct + 13;
            while (cs < headers.size() && (headers[cs] == ' ' || headers[cs] == '\t')) cs++;
            size_t ce = headers.find("\r\n", cs);
            if (ce == std::string::npos) ce = headers.size();
            part.content_type = headers.substr(cs, ce - cs);
        }
        size_t cd = headers.find("name=\"");
        if (cd != std::string::npos) {
            size_t ns = cd + 6;
            size_t ne = headers.find('"', ns);
            if (ne != std::string::npos) part.name = headers.substr(ns, ne - ns);
        }
        size_t fn = headers.find("filename=\"");
        if (fn != std::string::npos) {
            size_t fs = fn + 10;
            size_t fe = headers.find('"', fs);
            if (fe != std::string::npos) part.filename = headers.substr(fs, fe - fs);
        }
        part.data = std::move(data);
        parts.push_back(std::move(part));

        pos = body.find(boundary, data_start);
    }
    return true;
}

} // namespace util

// ═══════════════════════════════════════════════
//  ProtoBuf 编解码（完全与 Python sender.py 一致）
// ═══════════════════════════════════════════════
namespace proto {

inline Bytes to_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

// pb_varint: 与 Python SimpleProtobufCodec.encode_varint 一致
inline Bytes pb_varint(uint64_t v) {
    Bytes r;
    while (v > 0x7F) { r.push_back((uint8_t)((v & 0x7F) | 0x80)); v >>= 7; }
    r.push_back((uint8_t)(v & 0x7F));
    return r;
}

// pb_tag: 与 Python pb_tag(field, wire) 一致 — tag 也用 varint 编码
inline Bytes pb_tag(int fn, int wire) {
    return pb_varint((uint64_t)((fn << 3) | wire));
}

// pb_string: 与 Python pb_string(field, value) 一致
inline Bytes pb_string(int fn, const std::string& s) {
    Bytes d = to_bytes(s);
    Bytes r = pb_tag(fn, 2);
    auto lv = pb_varint(s.size());
    r.insert(r.end(), lv.begin(), lv.end());
    r.insert(r.end(), d.begin(), d.end());
    return r;
}

// pb_uint32: 与 Python pb_uint32(field, value) 一致
inline Bytes pb_uint32(int fn, uint32_t v) {
    Bytes r = pb_tag(fn, 0);
    auto vi = pb_varint(v);
    r.insert(r.end(), vi.begin(), vi.end());
    return r;
}

// pb_msg: 与 Python pb_msg(field, inner) 一致
inline Bytes pb_msg(int fn, const Bytes& inner) {
    Bytes r = pb_tag(fn, 2);
    auto lv = pb_varint(inner.size());
    r.insert(r.end(), lv.begin(), lv.end());
    r.insert(r.end(), inner.begin(), inner.end());
    return r;
}

// 兼容旧接口
inline Bytes encode_string(int fn, const std::string& s) { return pb_string(fn, s); }
inline Bytes encode_uint32(int fn, uint32_t v) { return pb_uint32(fn, v); }
inline Bytes encode_message(int fn, const Bytes& d) { return pb_msg(fn, d); }

// ==================== Protobuf 解码器 ====================
struct PBField {
    int fn = 0;
    int wire = 0;
    uint64_t varint_val = 0;
    std::string str_val;
    Bytes bytes_val;
    std::vector<PBField> sub_fields;  // 嵌套消息
};

// 从字节数组解码 varint，返回解码值和消耗的字节数
inline std::pair<uint64_t, size_t> decode_varint(const uint8_t* d, size_t max_len) {
    uint64_t v = 0; size_t i = 0; int shift = 0;
    while (i < max_len) { v |= ((uint64_t)(d[i] & 0x7F)) << shift; i++; if (!(d[i-1] & 0x80)) break; shift += 7; }
    return {v, i};
}

// 解析单个 protobuf 消息，返回字段列表
inline std::vector<PBField> decode_pb_message(const uint8_t* d, size_t len) {
    std::vector<PBField> fields;
    size_t pos = 0;
    while (pos < len) {
        auto [tag_raw, tag_len] = decode_varint(d + pos, len - pos);
        pos += tag_len;
        if (tag_raw == 0 && tag_len == 1 && d[pos - 1] == 0) break;
        int fn = (int)(tag_raw >> 3);
        int wire = (int)(tag_raw & 0x07);
        
        PBField f; f.fn = fn; f.wire = wire;
        if (wire == 0) {  // varint
            auto [v, vl] = decode_varint(d + pos, len - pos);
            f.varint_val = v; pos += vl;
        } else if (wire == 2) {  // length-delimited
            auto [vlen, vlen_len] = decode_varint(d + pos, len - pos);
            pos += vlen_len;
            size_t slen = (size_t)vlen;
            if (pos + slen <= len) {
                // 始终保留原始字节（字符串字段可能被误判为嵌套消息，导致 cmd 丢失）
                f.bytes_val = Bytes(d + pos, d + pos + slen);
                // 尝试作为嵌套消息解析
                auto sub = decode_pb_message(d + pos, slen);
                if (!sub.empty()) { f.sub_fields = std::move(sub); }
                pos += slen;
            }
        } else { break; }  // 不支持的类型
        
        fields.push_back(std::move(f));
    }
    return fields;
}

// 从字段列表中按字段号查找子字段
inline const PBField* find_field(const std::vector<PBField>& fields, int fn) {
    for (auto& f : fields) if (f.fn == fn) return &f;
    return nullptr;
}

// 解码后的 ConnMsg 结构
struct DecodedMsg {
    int cmd_type = 0;
    std::string cmd, msg_id, module;
    uint32_t seq_no = 0;
    bool need_ack = false;   // head field 6: push 是否要求 ACK
    int status = 0;          // head field 10
    Bytes data;  // field 2 的原始 bytes
};

// ── 入站消息 InboundMessagePush（protobuf）字段 ──
// 字段号对齐 openclaw-plugin 的 inbound_message.proto：
//   1=callback_command 2=from_account 3=to_account 4=sender_nickname 5=group_id
//   6=group_code 7=group_name 8=msg_seq 9=msg_random 10=msg_time 11=msg_key
//   12=msg_id 13=msg_body(repeated MsgBodyElement) 14=cloud_custom_data
//   15=event_time 16=bot_owner_id 17=recall_msg_seq_list 18=claw_msg_type
//   19=private_from_group_code 20=log_ext
struct InboundMsg {
    std::string callback_command, from_account, to_account, sender_nickname;
    std::string group_id, group_code, group_name, msg_key, msg_id;
    std::string cloud_custom_data, private_from_group_code;
    uint32_t msg_seq = 0, msg_random = 0, msg_time = 0, event_time = 0;
    int claw_msg_type = 0;
    bool has_recall = false;
    std::vector<PBField> body;   // repeated MsgBodyElement 原始字段
};

inline std::string pb_field_str(const PBField& f) {
    return f.bytes_val.empty() ? f.str_val : std::string(f.bytes_val.begin(), f.bytes_val.end());
}

inline InboundMsg parse_inbound_msg(const Bytes& d) {
    InboundMsg m;
    auto fields = decode_pb_message(d.data(), d.size());
    for (auto& f : fields) {
        switch (f.fn) {
            case 1:  m.callback_command = pb_field_str(f); break;
            case 2:  m.from_account = pb_field_str(f); break;
            case 3:  m.to_account = pb_field_str(f); break;
            case 4:  m.sender_nickname = pb_field_str(f); break;
            case 5:  m.group_id = pb_field_str(f); break;
            case 6:  m.group_code = pb_field_str(f); break;
            case 7:  m.group_name = pb_field_str(f); break;
            case 8:  m.msg_seq = (uint32_t)f.varint_val; break;
            case 9:  m.msg_random = (uint32_t)f.varint_val; break;
            case 10: m.msg_time = (uint32_t)f.varint_val; break;
            case 11: m.msg_key = pb_field_str(f); break;
            case 12: m.msg_id = pb_field_str(f); break;
            case 13: m.body.push_back(f); break;
            case 14: m.cloud_custom_data = pb_field_str(f); break;
            case 15: m.event_time = (uint32_t)f.varint_val; break;
            case 17: m.has_recall = true; break;
            case 18: m.claw_msg_type = (int)f.varint_val; break;
            case 19: m.private_from_group_code = pb_field_str(f); break;
            default: break;
        }
    }
    return m;
}

// 直接从二进制数据中按 tag 提取指定 field 的 bytes
inline Bytes extract_field_bytes(const uint8_t* d, size_t len, int target_fn, int target_wire = 2) {
    size_t pos = 0;
    while (pos < len) {
        auto [tag_raw, tag_len] = decode_varint(d + pos, len - pos);
        pos += tag_len;
        int fn = (int)(tag_raw >> 3);
        int wire = (int)(tag_raw & 0x07);
        
        if (wire == 0) {
            auto [v, vl] = decode_varint(d + pos, len - pos); pos += vl;
            if (fn == target_fn && target_wire == 0) return {(uint8_t)(v & 0xFF)};
        } else if (wire == 2) {
            auto [vlen, vlen_len] = decode_varint(d + pos, len - pos);
            pos += vlen_len;
            size_t slen = (size_t)vlen;
            if (pos + slen <= len) {
                if (fn == target_fn && target_wire == 2) return Bytes(d + pos, d + pos + slen);
                pos += slen;
            } else break;
        } else { break; }
    }
    return {};
}

inline DecodedMsg decode_conn_msg(const Bytes& payload) {
    DecodedMsg m;
    
    // 方式1: 有 ConnMsg 外包装 (field 1 = Head, field 2 = Data)
    auto head_bytes = extract_field_bytes(payload.data(), payload.size(), 1);
    if (!head_bytes.empty()) {
        auto hf = decode_pb_message(head_bytes.data(), head_bytes.size());
        auto* ct = find_field(hf, 1); if (ct) m.cmd_type = (int)ct->varint_val;
        auto* cm = find_field(hf, 2); if (cm) m.cmd = cm->bytes_val.empty() ? cm->str_val : std::string(cm->bytes_val.begin(), cm->bytes_val.end());
        auto* sn = find_field(hf, 3); if (sn) m.seq_no = (uint32_t)sn->varint_val;
        auto* mi = find_field(hf, 4); if (mi) m.msg_id = mi->bytes_val.empty() ? mi->str_val : std::string(mi->bytes_val.begin(), mi->bytes_val.end());
        auto* mo = find_field(hf, 5); if (mo) m.module = mo->bytes_val.empty() ? mo->str_val : std::string(mo->bytes_val.begin(), mo->bytes_val.end());
        m.data = extract_field_bytes(payload.data(), payload.size(), 2);
        return m;
    }
    
    // 方式2: 无 ConnMsg 外包装，数据直接就是 Head
    // (某些响应如 auth-bind 响应直接发送 Head)
    auto hf = decode_pb_message(payload.data(), payload.size());
    auto* ct = find_field(hf, 1); if (ct) m.cmd_type = (int)ct->varint_val;
    auto* cm = find_field(hf, 2); if (cm) m.cmd = cm->bytes_val.empty() ? cm->str_val : std::string(cm->bytes_val.begin(), cm->bytes_val.end());
    auto* sn = find_field(hf, 3); if (sn) m.seq_no = (uint32_t)sn->varint_val;
    auto* mi = find_field(hf, 4); if (mi) m.msg_id = mi->bytes_val.empty() ? mi->str_val : std::string(mi->bytes_val.begin(), mi->bytes_val.end());
    auto* mo = find_field(hf, 5); if (mo) m.module = mo->bytes_val.empty() ? mo->str_val : std::string(mo->bytes_val.begin(), mo->bytes_val.end());
    auto* na = find_field(hf, 6); if (na) m.need_ack = (na->varint_val != 0);
    auto* stt = find_field(hf, 10); if (stt) m.status = (int)stt->varint_val;
    
    return m;
}

inline std::string base64_encode(const unsigned char* d, size_t len) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    for (size_t i = 0; i < len; i += 3) {
        int b = (d[i] << 16);
        if (i + 1 < len) b |= d[i + 1] << 8;
        if (i + 2 < len) b |= d[i + 2];
        r += b64[(b >> 18) & 63];
        r += b64[(b >> 12) & 63];
        r += i + 1 < len ? b64[(b >> 6) & 63] : '=';
        r += i + 2 < len ? b64[b & 63] : '=';
    }
    return r;
}

struct WSFrame {
    bool fin = true;
    uint8_t opcode = 1;
    bool mask = false;
    size_t payload_len = 0;
    Bytes payload;
    Bytes mask_key;
};

inline WSFrame parse_ws_frame(const uint8_t* data, size_t len, size_t& pos) {
    WSFrame f;
    pos = 0;
    if (pos + 2 > len) return f;
    f.fin = (data[pos] & 0x80) != 0;
    f.opcode = data[pos] & 0x0F;
    pos++;
    uint8_t b = data[pos++];
    f.mask = (b & 0x80) != 0;
    f.payload_len = b & 0x7F;
    if (f.payload_len == 126) {
        if (pos + 2 > len) { pos = 0; return f; }
        f.payload_len = ((size_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
    } else if (f.payload_len == 127) {
        if (pos + 8 > len) { pos = 0; return f; }
        f.payload_len = 0;
        for (int i = 0; i < 8; i++)
            f.payload_len = (f.payload_len << 8) | data[pos++];
    }
    if (f.mask && pos + 4 > len) { pos = 0; return f; }  // 数据不足
    if (f.mask) {
        f.mask_key = Bytes(data + pos, data + pos + 4);
        pos += 4;
    }
    if (pos + f.payload_len > len) { pos = 0; return f; }  // 数据不足，等更多数据
    f.payload = Bytes(data + pos, data + pos + f.payload_len);
    pos += f.payload_len;
    if (f.mask)
        for (size_t i = 0; i < f.payload.size(); i++)
            f.payload[i] ^= f.mask_key[i % 4];
    return f;
}

inline Bytes build_ws_frame(uint8_t opcode, const Bytes& payload, bool client_frame = true) {
    Bytes frame;
    frame.push_back((uint8_t)(0x80 | opcode));
    size_t plen = payload.size();
    if (client_frame) {
        // 客户端帧必须 mask
        if (plen < 126) {
            frame.push_back((uint8_t)(plen | 0x80));
        } else if (plen < 65536) {
            frame.push_back((uint8_t)(126 | 0x80));
            frame.push_back((uint8_t)((plen >> 8) & 0xFF));
            frame.push_back((uint8_t)(plen & 0xFF));
        } else {
            frame.push_back((uint8_t)(127 | 0x80));
            for (int i = 7; i >= 0; i--)
                frame.push_back((uint8_t)((plen >> (i * 8)) & 0xFF));
        }
        // 生成 4 字节 mask key
        uint8_t mask[4];
        static std::mt19937 rng(std::random_device{}());
        for (int i = 0; i < 4; i++) mask[i] = (uint8_t)(rng() & 0xFF);
        frame.insert(frame.end(), mask, mask + 4);
        // mask 数据
        for (size_t i = 0; i < plen; i++)
            frame.push_back(payload[i] ^ mask[i % 4]);
    } else {
        if (plen < 126) {
            frame.push_back((uint8_t)plen);
        } else if (plen < 65536) {
            frame.push_back(126);
            frame.push_back((uint8_t)((plen >> 8) & 0xFF));
            frame.push_back((uint8_t)(plen & 0xFF));
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--)
                frame.push_back((uint8_t)((plen >> (i * 8)) & 0xFF));
        }
        frame.insert(frame.end(), payload.begin(), payload.end());
    }
    return frame;
}

// ── 消息编码辅助 ──
inline Bytes encode_conn_head(int cmd_type, const std::string& cmd,
                              uint32_t seq_no, const std::string& msg_id,
                              const std::string& module) {
    Bytes r;
    bytes_append(r, encode_uint32(1, (uint32_t)cmd_type));
    bytes_append(r, encode_string(2, cmd));
    bytes_append(r, encode_uint32(3, seq_no));
    bytes_append(r, encode_string(4, msg_id));
    bytes_append(r, encode_string(5, module));
    return r;
}

inline Bytes encode_conn_msg(const Bytes& head, const Bytes& body = {}) {
    Bytes r = encode_message(1, head);
    if (!body.empty()) bytes_append(r, encode_message(2, body));
    return r;
}

inline Bytes encode_text_elem(const std::string& text) {
    Bytes content = encode_string(1, text);
    Bytes elem = encode_string(1, "TIMTextElem");
    bytes_append(elem, encode_message(2, content));
    return elem;
}

inline Bytes encode_face_elem(const std::string& sticker_json) {
    Bytes mc;
    bytes_append(mc, encode_uint32(9, 0));
    bytes_append(mc, encode_string(4, sticker_json));
    Bytes elem = encode_string(1, "TIMFaceElem");
    bytes_append(elem, encode_message(2, mc));
    return elem;
}

inline Bytes encode_image_elem(const std::string& url, const std::string& uuid = "",
                               int size = 0, int w = 0, int h = 0) {
    // 与 Python encode_tim_image_elem 完全一致
    Bytes img_info;
    bytes_append(img_info, encode_uint32(1, 1));
    bytes_append(img_info, encode_uint32(2, (uint32_t)size));
    bytes_append(img_info, encode_uint32(3, (uint32_t)w));
    bytes_append(img_info, encode_uint32(4, (uint32_t)h));
    bytes_append(img_info, encode_string(5, url));
    Bytes mc;
    // ← 对齐 Python 实际发图路径 _build_image_elem（sender.py 1860-1875）：
    //   msg_content: field 2=uuid, field 3=image_format, field 8=image_info_array
    if (!uuid.empty()) bytes_append(mc, encode_string(2, uuid));
    bytes_append(mc, encode_uint32(3, 255));
    bytes_append(mc, encode_message(8, img_info));
    Bytes elem = encode_string(1, "TIMImageElem");
    bytes_append(elem, encode_message(2, mc));
    return elem;
}

inline Bytes encode_file_elem(const std::string& url, const std::string& uuid = "",
                              int file_size = 0, const std::string& file_name = "") {
    // 与 Python encode_tim_file_elem 完全一致
    Bytes mc;
    if (!uuid.empty()) bytes_append(mc, encode_string(2, uuid));
    bytes_append(mc, encode_string(10, url));
    if (file_size > 0) bytes_append(mc, encode_uint32(11, (uint32_t)file_size));
    if (!file_name.empty()) bytes_append(mc, encode_string(12, file_name));
    Bytes elem = encode_string(1, "TIMFileElem");
    bytes_append(elem, encode_message(2, mc));
    return elem;
}

/** JSON 字符串转义（ensure_ascii 模式，与 Python json.dumps 默认一致） */
inline std::string json_escape_str(const std::string& s) {
    static const char* hexdig = "0123456789abcdef";
    std::string r;
    r.reserve(s.size() + 8);
    size_t i = 0;
    auto append_hex4 = [&](uint32_t cp) {
        r += "\\u";
        r += hexdig[(cp >> 12) & 0xF];
        r += hexdig[(cp >> 8) & 0xF];
        r += hexdig[(cp >> 4) & 0xF];
        r += hexdig[cp & 0xF];
    };
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') { r += "\\\""; i++; }
        else if (c == '\\') { r += "\\\\"; i++; }
        else if (c == '\n') { r += "\\n"; i++; }
        else if (c == '\r') { r += "\\r"; i++; }
        else if (c == '\t') { r += "\\t"; i++; }
        else if (c < 0x20) { append_hex4(c); i++; }
        else if (c < 0x80) { r += (char)c; i++; }
        else {
            // UTF-8 解码 → Unicode 码点，再 \uXXXX 转义（Python ensure_ascii）
            uint32_t cp = 0;
            int n = 0;
            if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
            else { append_hex4(c); i++; continue; }
            bool ok = true;
            for (int k = 1; k <= n; k++) {
                if (i + (size_t)k >= s.size()) { ok = false; break; }
                unsigned char cc = (unsigned char)s[i + (size_t)k];
                if ((cc & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            if (!ok) { append_hex4(c); i++; continue; }
            if (cp < 0x10000) append_hex4(cp);
            else {
                // 增补平面：编码为代理对（与 Python json.dumps 一致）
                cp -= 0x10000;
                uint32_t hi = 0xD800 + (cp >> 10);
                uint32_t lo = 0xDC00 + (cp & 0x3FF);
                append_hex4(hi); append_hex4(lo);
            }
            i += (size_t)n + 1;
        }
    }
    return r;
}

/** @某人的 TIMCustomElem（与 Python _build_at_message 完全一致） */
inline Bytes encode_at_custom_elem(const std::string& user_id, const std::string& display_name = "") {
    std::string disp = display_name.empty() ? user_id : display_name;
    // Python: json.dumps({"elem_type": 1002, "text": f"@{display_name}", "user_id": at_user_id})
    // 默认 ensure_ascii=True + 带空格分隔符
    std::string at_data = "{\"elem_type\": 1002, \"text\": \"@" + json_escape_str(disp)
                          + "\", \"user_id\": \"" + json_escape_str(user_id) + "\"}";
    Bytes content = encode_string(4, at_data);
    Bytes elem = encode_string(1, "TIMCustomElem");
    bytes_append(elem, encode_message(2, content));
    return elem;
}

/** @某人 + 文本 群消息（与 Python _build_at_message 一致，多个 field 6） */
inline Bytes encode_send_group_at_msg(const std::string& msg_id,
                                      const std::string& group_code,
                                      const std::string& from_account,
                                      const std::string& text,
                                      const std::string& user_id,
                                      const std::string& display_name = "") {
    Bytes body;
    bytes_append(body, encode_string(1, msg_id));
    bytes_append(body, encode_string(2, group_code));
    bytes_append(body, encode_string(3, from_account));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 4294967295);
    bytes_append(body, encode_string(5, std::to_string(rnd)));
    bytes_append(body, encode_message(6, encode_at_custom_elem(user_id, display_name)));
    // ← 修复：text 为空（纯 @ 不跟内容）时不再追加空文本元素，与 Python 一致
    if (!text.empty()) bytes_append(body, encode_message(6, encode_text_elem(text)));
    return body;
}

/** 有序消息片段：type 0=文本, 1=@（保持 @ 在输入时的原始位置） */
struct SendPart {
    int type = 0;          // 0=text, 1=at
    std::string text;      // type=0 时的文本
    std::string user_id;   // type=1 时的被 @ 用户
    std::string display;   // type=1 时的展示昵称
};

/** 按顺序编码 text/at 片段为群消息（@ 位置保持，多个 field 6） */
inline Bytes encode_send_group_parts_msg(const std::string& msg_id,
                                         const std::string& group_code,
                                         const std::string& from_account,
                                         const std::vector<SendPart>& parts) {
    Bytes body;
    bytes_append(body, encode_string(1, msg_id));
    bytes_append(body, encode_string(2, group_code));
    bytes_append(body, encode_string(3, from_account));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 4294967295);
    bytes_append(body, encode_string(5, std::to_string(rnd)));
    for (auto& p : parts) {
        if (p.type == 1)
            bytes_append(body, encode_message(6, encode_at_custom_elem(p.user_id, p.display)));
        else if (!p.text.empty())
            bytes_append(body, encode_message(6, encode_text_elem(p.text)));
    }
    return body;
}

/** 批量 @ 多人 + 文本 群消息（与 Python _build_multi_at_message 一致） */
inline Bytes encode_send_group_multi_at_msg(const std::string& msg_id,
                                            const std::string& group_code,
                                            const std::string& from_account,
                                            const std::string& text,
                                            const std::vector<std::pair<std::string, std::string>>& at_users) {
    Bytes body;
    bytes_append(body, encode_string(1, msg_id));
    bytes_append(body, encode_string(2, group_code));
    bytes_append(body, encode_string(3, from_account));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 4294967295);
    bytes_append(body, encode_string(5, std::to_string(rnd)));
    for (auto& u : at_users)
        bytes_append(body, encode_message(6, encode_at_custom_elem(u.first, u.second)));
    // ← 修复：text 为空（纯 @ 不跟内容）时不再追加空文本元素，与 Python 一致
    if (!text.empty()) bytes_append(body, encode_message(6, encode_text_elem(text)));
    return body;
}

inline Bytes encode_send_group_msg(const std::string& msg_id,
                                    const std::string& group_code,
                                    const std::string& from_account,
                                    const std::string& text,
                                    const std::string& ref_msg_id = "",
                                    const std::string& at_user = "",
                                    const std::string& at_nick = "") {
    // 与 Python encode_send_group_req / _build_reply_msg(at) 一致
    Bytes body;
    bytes_append(body, encode_string(1, msg_id));
    bytes_append(body, encode_string(2, group_code));
    bytes_append(body, encode_string(3, from_account));
    bytes_append(body, encode_string(4, ""));  // 空字符串
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 4294967295);
    bytes_append(body, encode_string(5, std::to_string(rnd)));  // string 类型!
    if (!at_user.empty())
        bytes_append(body, encode_message(6, encode_at_custom_elem(at_user, at_nick)));
    bytes_append(body, encode_message(6, encode_text_elem(text)));
    bytes_append(body, encode_string(7, ref_msg_id));  // 引用消息ID（或空）
    return body;
}

inline Bytes encode_send_c2c_msg(const std::string& msg_id,
                                  const std::string& to_account,
                                  const std::string& from_account,
                                  const std::string& text) {
    Bytes body;
    bytes_append(body, encode_string(1, msg_id));
    bytes_append(body, encode_string(2, to_account));
    bytes_append(body, encode_string(3, from_account));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000);
    bytes_append(body, encode_uint32(4, rnd));
    bytes_append(body, encode_message(5, encode_text_elem(text)));
    return body;
}

inline Bytes encode_sticker_group_msg(const std::string& msg_id,
                                       const std::string& group_code,
                                       const std::string& from_account,
                                       const std::string& sticker_json,
                                       const std::string& at_user = "",
                                       const std::string& at_nick = "",
                                       const std::string& text = "") {
    // 与 Python _build_sticker_msg / _build_sticker_with_at_msg 一致：
    // field 1 msg_id / 2 group / 3 from / 5 random(str) / 6 元素列表（无 field 4、7）
    Bytes body;
    bytes_append(body, encode_string(1, msg_id));
    bytes_append(body, encode_string(2, group_code));
    bytes_append(body, encode_string(3, from_account));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000);
    bytes_append(body, encode_string(5, std::to_string(rnd)));  // string 类型，与文字消息一致
    if (!at_user.empty())
        bytes_append(body, encode_message(6, encode_at_custom_elem(at_user, at_nick)));
    bytes_append(body, encode_message(6, encode_face_elem(sticker_json)));
    if (!text.empty())
        bytes_append(body, encode_message(6, encode_text_elem(text)));
    return body;
}

inline Bytes encode_sticker_c2c_msg(const std::string& msg_id,
                                    const std::string& to_account,
                                    const std::string& from_account,
                                    const std::string& sticker_json) {
    // 私聊 SendC2CMessageReq：1 msg_id / 2 to_account / 3 from_account / 4 random(uint32) / 5 元素列表
    Bytes body;
    bytes_append(body, encode_string(1, msg_id));
    bytes_append(body, encode_string(2, to_account));
    bytes_append(body, encode_string(3, from_account));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000);
    bytes_append(body, encode_uint32(4, rnd));
    bytes_append(body, encode_message(5, encode_face_elem(sticker_json)));
    return body;
}

inline Bytes encode_auth_bind(const std::string& bot_id, const std::string& token,
                              const std::string& source = "web") {
    // bizId="ybBot" + authInfo{uid,source,token} + deviceInfo
    // 注意：source 必须与 sign-token 签发时绑定的一致，否则服务端返回 20001 token source not match
    Bytes auth_info;
    bytes_append(auth_info, encode_string(1, bot_id));   // uid
    bytes_append(auth_info, encode_string(2, source.empty() ? "web" : source));
    bytes_append(auth_info, encode_string(3, token));
    // deviceInfo: 1=app_version, 2=app_operation_system, 10=instance_id(16=OpenClaw 终端类型)
#ifdef YUANBAO_PLATFORM_WINDOWS
    const char* os_name = "Windows_NT";
#else
    const char* os_name = "Linux";
#endif
    Bytes device_info;
    bytes_append(device_info, encode_string(1, "1.0.0"));
    bytes_append(device_info, encode_string(2, os_name));
    bytes_append(device_info, encode_string(10, "16"));
    Bytes data;
    bytes_append(data, encode_string(1, "ybBot"));
    bytes_append(data, encode_message(2, auth_info));
    bytes_append(data, encode_message(3, device_info));
    return data;
}

// ── 回复状态心跳（EnumHeartbeat: RUNNING=1 / FINISH=2）──
// SendGroupHeartbeatReq: 1=from_account 2=to_account 3=group_code 4=send_time(int64) 5=heartbeat
inline Bytes encode_send_group_heartbeat(const std::string& from, const std::string& to,
                                         const std::string& group, int64_t send_time, int heartbeat) {
    Bytes d;
    bytes_append(d, encode_string(1, from));
    bytes_append(d, encode_string(2, to));
    bytes_append(d, encode_string(3, group));
    Bytes st = pb_tag(4, 0);
    auto sv = pb_varint((uint64_t)send_time);
    st.insert(st.end(), sv.begin(), sv.end());
    bytes_append(d, st);
    bytes_append(d, pb_uint32(5, (uint32_t)heartbeat));
    return d;
}

// SendPrivateHeartbeatReq: 1=from_account 2=to_account 3=heartbeat
inline Bytes encode_send_private_heartbeat(const std::string& from, const std::string& to, int heartbeat) {
    Bytes d;
    bytes_append(d, encode_string(1, from));
    bytes_append(d, encode_string(2, to));
    bytes_append(d, pb_uint32(3, (uint32_t)heartbeat));
    return d;
}

} // namespace proto

// ═══════════════════════════════════════════════
//  SHA1 手写实现 (无外部依赖)
// ═══════════════════════════════════════════════
inline void sha1_raw(const uint8_t* input, size_t ilen, uint8_t output[20]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    // ← 修复：SHA1 用循环左移（ROTL），原实现误用右旋导致所有 SHA1 结果错误
    auto rotl = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };

    size_t padded = ((ilen + 8) / 64 + 1) * 64;
    std::vector<uint8_t> dat(padded, 0);
    memcpy(dat.data(), input, ilen);
    dat[ilen] = 0x80;
    uint64_t bits = ilen * 8;
    for (int i = 0; i < 8; i++)
        dat[padded - 1 - i] = (uint8_t)((bits >> (i * 8)) & 0xFF);

    for (size_t chunk = 0; chunk < padded; chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)dat[chunk + i * 4] << 24) | ((uint32_t)dat[chunk + i * 4 + 1] << 16) |
                   ((uint32_t)dat[chunk + i * 4 + 2] << 8) | dat[chunk + i * 4 + 3];
        for (int i = 16; i < 80; i++)
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    for (int i = 0; i < 5; i++) {
        output[i * 4]     = (uint8_t)((h[i] >> 24) & 0xFF);
        output[i * 4 + 1] = (uint8_t)((h[i] >> 16) & 0xFF);
        output[i * 4 + 2] = (uint8_t)((h[i] >> 8) & 0xFF);
        output[i * 4 + 3] = (uint8_t)(h[i] & 0xFF);
    }
}

inline std::string sha1_hex(const std::string& s) {
    uint8_t out[20];
    sha1_raw((const uint8_t*)s.data(), s.size(), out);
    char buf[41];
    for (int i = 0; i < 20; i++) sprintf(buf + i * 2, "%02x", out[i]);
    return std::string(buf, 40);
}

/** HMAC-SHA1 十六进制（基于手写 SHA1，无外部依赖；用于腾讯云 COS 签名） */
inline std::string hmac_sha1_hex(const std::string& key, const std::string& data) {
    uint8_t k[64] = {0};
    size_t keylen = key.size();
    if (keylen > 64) {
        uint8_t kh[20];
        sha1_raw((const uint8_t*)key.data(), keylen, kh);
        memcpy(k, kh, 20);
    } else {
        memcpy(k, key.data(), keylen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    std::vector<uint8_t> inner;
    inner.insert(inner.end(), ipad, ipad + 64);
    inner.insert(inner.end(), data.begin(), data.end());
    uint8_t ih[20];
    sha1_raw(inner.data(), inner.size(), ih);
    std::vector<uint8_t> outer;
    outer.insert(outer.end(), opad, opad + 64);
    outer.insert(outer.end(), ih, ih + 20);
    uint8_t oh[20];
    sha1_raw(outer.data(), outer.size(), oh);
    char buf[41];
    for (int i = 0; i < 20; i++) sprintf(buf + i * 2, "%02x", oh[i]);
    return std::string(buf, 40);
}

// ═══════════════════════════════════════════════
//  JSON (轻量无依赖实现)
// ═══════════════════════════════════════════════
struct JsonVal {
    std::variant<std::monostate, std::string, double, bool> v;
    std::map<std::string, JsonVal> obj;
    std::vector<JsonVal> arr;

    bool is_string() const { return v.index() == 1; }
    bool is_number() const { return v.index() == 2; }
    bool is_bool() const   { return v.index() == 3; }
    bool is_null() const   { return v.index() == 0; }
    bool empty() const     { return obj.empty() && arr.empty() && v.index() == 0; }

    std::string asString() const { return is_string() ? std::get<1>(v) : ""; }
    int         asInt()    const { return (int)asDouble(); }
    double      asDouble() const { return is_number() ? std::get<2>(v) : 0.0; }
    bool        asBool()   const { return is_bool() ? std::get<3>(v) : false; }

    const JsonVal& operator[](const std::string& k) const {
        static JsonVal nil;
        auto it = obj.find(k);
        return it != obj.end() ? it->second : nil;
    }
    JsonVal& operator[](const std::string& k) { return obj[k]; }

    const JsonVal& operator[](size_t i) const {
        static JsonVal nil;
        return i < arr.size() ? arr[i] : nil;
    }
    JsonVal& operator[](size_t i) {
        if (i >= arr.size()) arr.resize(i + 1);
        return arr[i];
    }

    size_t size() const { return arr.empty() ? obj.size() : arr.size(); }

    JsonVal& operator=(const std::string& s) { v = s; obj.clear(); arr.clear(); return *this; }
    JsonVal& operator=(const char* s)        { v = std::string(s ? s : ""); obj.clear(); arr.clear(); return *this; }
    JsonVal& operator=(double d)             { v = d; obj.clear(); arr.clear(); return *this; }
    JsonVal& operator=(bool b)               { v = b; obj.clear(); arr.clear(); return *this; }
    JsonVal& operator=(int i)                { v = (double)i; obj.clear(); arr.clear(); return *this; }
};

std::string json_quote(const std::string& s);
std::string json_compact(const JsonVal& v);
bool        json_parse(const std::string& s, JsonVal& out);

// ═══════════════════════════════════════════════
//  HTTP 请求 / 响应
// ═══════════════════════════════════════════════
struct HttpRequest {
    std::string method, path, version, body;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
};

struct HttpResponse {
    std::string version = "HTTP/1.1";
    int status = 200;
    std::string status_text = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
    Bytes body_bytes;

    HttpResponse() {}
    HttpResponse& status_code(int s, const std::string& t = "") {
        status = s; status_text = t; return *this;
    }
    HttpResponse& json(const std::string& b) {
        body = b;
        headers["Content-Type"] = "application/json; charset=utf-8";
        headers["Access-Control-Allow-Origin"] = "*";
        return *this;
    }
    HttpResponse& js(const std::string& b) {
        body = b;
        headers["Content-Type"] = "application/javascript; charset=utf-8";
        return *this;
    }
    HttpResponse& html(const std::string& b) {
        body = b;
        headers["Content-Type"] = "text/html; charset=utf-8";
        return *this;
    }
    HttpResponse& css(const std::string& c) {
        body = c;
        headers["Content-Type"] = "text/css; charset=utf-8";
        return *this;
    }
    HttpResponse& not_found() {
        return status_code(404, "Not Found").json("{\"error\":\"not found\"}");
    }
    HttpResponse& bad_request() {
        return status_code(400, "Bad Request").json("{\"error\":\"bad request\"}");
    }
};


// ═══════════════════════════════════════════════
//  SSE Broker
// ═══════════════════════════════════════════════
// ═══════════════════════════════════════════════
//  消息日志
// ═══════════════════════════════════════════════
class MessageLogger {
public:
    std::string base_dir;
    std::atomic<uint64_t> total_written{0};
    std::atomic<bool> running_{true};
    std::atomic<bool> enabled{true};   // 消息记录总开关（可在设置中动态切换）
    mutable std::mutex mu_;
    std::deque<std::string> queue_;
    std::thread writer_;
    std::string today_date_;

    MessageLogger(const std::string& dir = "./logs");
    ~MessageLogger();

    void log(const JsonVal& msg);
    void log_raw(const std::string& line);
    void stop();
    void clear_all();   // 清空待写队列并删除所有已写入的日志文件

    size_t pending_count() const;
    int64_t today_jsonl_size() const;
    int64_t today_txt_size() const;
    int64_t total_size() const;

private:
    std::string current_date();
    void write_loop();
    void flush(const std::vector<std::string>& buf);
};

// ═══════════════════════════════════════════════
//  刷屏引擎
// ═══════════════════════════════════════════════
namespace flood {

enum class FloodMode {
    RANDOM = 0, FULLWIDTH, MOCK, ZALGO, REPEAT, ALTERNATE, EMOJI,
    GHOST, MATRIX, EARTHQUAKE, BUBBLE, FIREWORK, RAINBOW, STORM, TSUNAMI, QUANTUM
};

struct FloodTask {
    std::string text;
    int count = 10;
    int delay_ms = 50;
    int batch_size = 3;
    FloodMode mode = FloodMode::RANDOM;
    std::string group_code;
    std::atomic<bool> cancelled{false};
    uint64_t created_at = 0;
    std::atomic<int> sent{0};
    std::atomic<bool> running{false};

    // atomic 成员不可拷贝，需显式定义
    FloodTask() = default;
    FloodTask(const FloodTask& o)
        : text(o.text), count(o.count), delay_ms(o.delay_ms),
          batch_size(o.batch_size), mode(o.mode), group_code(o.group_code),
          cancelled(o.cancelled.load()), created_at(o.created_at),
          sent(o.sent.load()), running(o.running.load()) {}
};

struct FloodStats {
    int total_tasks = 0;
    int total_sent = 0;
    int active_tasks = 0;
    double avg_speed = 0;
};

std::string transform_text(const std::string& text, FloodMode mode);

class FloodEngine {
public:
    std::function<void(const std::string& group, const std::string& msg)> sender_;
    mutable std::mutex mu_;
    std::map<std::string, std::shared_ptr<FloodTask>> tasks_;
    FloodStats stats_;

    std::string create_task(const FloodTask& task);
    void cancel_task(const std::string& id);
    std::string get_task_status(const std::string& id);
    std::string list_tasks();
    FloodStats get_stats() const;
};

} // namespace flood

// ═══════════════════════════════════════════════
//  贴纸库
// ═══════════════════════════════════════════════
class StickerLibrary {
public:
    std::map<std::string, std::string> stickers;
    StickerLibrary();
    std::string find_by_name(const std::string& name) const;
};

// ═══════════════════════════════════════════════
//  大模型 API 配置
// ═══════════════════════════════════════════════
// ═══════════════════════════════════════════════
//  MCP（Model Context Protocol）客户端 / Skill（OpenClaw 兼容）
// ═══════════════════════════════════════════════
struct McpTool {
    std::string name;
    std::string description;
    std::string input_schema;    // JSON Schema（原样透传给模型参考）
};

struct McpServer {
    std::string name;                        // 逻辑名（调用时用 name.tool）
    std::string command;                     // 启动命令，如 npx
    std::vector<std::string> args;           // 参数
    std::map<std::string, std::string> env;  // 环境变量
    std::vector<McpTool> tools;              // tools/list 结果缓存
    bool ready = false;                      // 是否成功完成握手
};

struct SkillInfo {
    std::string name;
    std::string description;
    std::string body;     // SKILL.md 正文（Markdown 指令）
    std::string path;
};

struct LLMConfig {
    std::string api_url;        // API 地址，兼容 OpenAI/DeepSeek/通义千问等
    std::string api_key;        // API Key
    std::string model;          // 模型名称
    std::string system_prompt;  // 系统提示词
    int max_tokens = 200;       // 最大输出 token 数
    // 模型返回空时的兜底回复（对齐官方插件 fallbackReply）：默认开启，可用 LLM_FALLBACK_ENABLED=false 关闭
    bool fallback_enabled = true;
    std::string fallback_text = "（我这边暂时没想出合适的回复，稍后再试一次吧～）";
    double temperature = 0.8;   // 温度参数
    int timeout_sec = 15;       // 请求超时（秒）
    bool enabled = false;       // 是否启用大模型回复（api_key 非空时自动启用）
};

// ═══════════════════════════════════════════════
//  Bot 配置
// ═══════════════════════════════════════════════
// 单个 Bot 账号（多 APP_KEY/APP_SECRET 支持）
struct BotAccountEntry {
    std::string name;                     // 账号名（ACCOUNTS[].NAME）
    std::string app_key, app_secret;
    std::string yuanbao_id;
    std::vector<std::string> listen_groups;
    // 账号级 AI 配置（可选，覆盖全局 LLM_*）
    LLMConfig llm;
    bool has_llm = false;
};

struct BotConfig {
    std::string app_key, app_secret, api_domain, ws_url;
    std::string yuanbao_id;
    // 多群监听：所有被监听群，第一个为默认目标群（发送/转发/查询的默认目标）
    std::vector<std::string> listen_groups;
    int heartbeat_interval = 10;
    int port = 5000;   // 遗留字段（旧 Web 服务端口），纯 CLI 已不再使用
    LLMConfig llm;
    bool msg_log_enabled = true;         // 消息记录开关
    // 被 @ 判定：平台未下发 @ 元素 user_id 时，是否退回"@昵称"文本兜底（默认关闭，避免同名机器人误触发）
    bool at_nickname_fallback = false;
    // AI 回复的引用模式（对齐官方 replyToMode）：
    //   off=不引用 / first=仅首条引用触发消息（默认）/ all=每条都引用
    std::string reply_to_mode = "first";

    // ── 命令执行 / 文件读写（⚠️ 高风险：等于把本机开放为远程 Shell）──
    // 默认全部关闭，必须显式在 config.json 打开
    bool exec_enabled = false;                  // EXEC_ENABLED：总开关
    // EXEC_SCOPE：owner=仅 OWNER_ID / private=仅私聊(任何人) / all=私聊+群聊
    std::string exec_scope = "owner";
    std::string owner_id;                       // OWNER_ID：Bot 创建者用户 ID（EXEC_SCOPE=owner 时用）
    std::vector<std::string> exec_allow_from;   // EXEC_ALLOW_FROM：额外放行的发送者（空=不限制）
    int exec_timeout_sec = 30;                  // EXEC_TIMEOUT：命令超时秒数（超时后仍会尽力回执）
    std::string exec_cwd;                       // EXEC_CWD：命令与相对路径的工作目录（空=当前目录）
    bool ai_exec_enabled = false;               // AI_EXEC_ENABLED：允许模型输出 [exec:命令] 自主执行
    bool file_ops_enabled = false;              // FILE_OPS_ENABLED：/read /write /append /ls
    bool exec_deny_enabled = true;              // EXEC_DENY_ENABLED：内置危险命令黑名单（建议保持开启）
    // EXEC_REPLY_MODE：执行结果回执位置 —— origin=回到触发会话（默认）/ private=只私聊给触发者（群里仅提示）
    std::string exec_reply_mode = "origin";
    // UPGRADE_SOURCE_URL：/yuanbao-upgrade 的源码下载地址（指向最新 main.cpp 的 raw URL）；
    // 留空则用本地源码重新编译（应用本地改动 / 修复损坏的 exe）
    std::string upgrade_source_url;

    // ── 文件写入保护 ──
    // FILE_ROOTS：允许写入的根目录列表（空 = 除系统目录外均可写）
    // FILE_DENY_SYSTEM：拒绝写入系统目录（默认开启；Windows: C:\Windows、Program Files…；
    //                   Linux: /etc、/usr、/bin…）
    std::vector<std::string> file_roots;
    bool file_deny_system = true;

    // ── MCP 客户端 / Skill / 子代理 ──
    // MCP_SERVERS：[{name, command, args[], env{}}]
    std::vector<McpServer> mcp_servers;
    // SKILLS_DIRS：额外扫描的 skill 目录（默认还会扫 ./skills）
    std::vector<std::string> skills_dirs;
    // SUBAGENT_MAX_CONCURRENT / SUBAGENT_TIMEOUT_SEC
    int subagent_max_concurrent = 4;
    int subagent_timeout_sec = 120;
    bool mcp_enabled = true;      // MCP_ENABLED（总开关）
    bool skills_enabled = true;   // SKILLS_ENABLED

    // ── 联网搜索 / 网页抓取 ──
    bool web_search_enabled = true;   // WEB_SEARCH_ENABLED
    std::string search_api_url;       // SEARCH_API_URL（Tavily 兼容；留空则抓 Bing HTML）
    std::string search_api_key;       // SEARCH_API_KEY
    int search_count = 5;             // SEARCH_COUNT（1-10）
    bool web_fetch_enabled = true;    // WEB_FETCH_ENABLED（[fetch:URL] 抓正文）

    // AI 上下文记忆：每个会话保留最近 N 条消息（含 bot 自己的回复）喂给大模型
    bool ai_ctx_enabled = true;
    int ai_ctx_messages = 100;

    // ── 多账号 ──
    std::string account_name;                 // 当前选中的账号名（顶层默认账号为 "default"）
    std::vector<BotAccountEntry> accounts;    // config 中全部账号（含顶层默认账号）

    bool load(const std::string& path);
    // account 为空/ "default" → 顶层默认账号；否则按 ACCOUNTS[].NAME 或序号选择
    bool load(const std::string& path, const std::string& account);

    // 默认目标群：监听列表第一项（主群概念已移除，无监听群时返回空串）
    const std::string& default_target() const {
        static const std::string empty;
        return listen_groups.empty() ? empty : listen_groups.front();
    }

    // 是否监听指定群
    bool is_listening(const std::string& group) const {
        if (group.empty()) return false;
        for (auto& g : listen_groups) if (g == group) return true;
        return false;
    }

    // 全部账号名（供 CLI 枚举）
    std::vector<std::string> account_name_list() const {
        std::vector<std::string> names;
        for (auto& a : accounts) names.push_back(a.name);
        return names;
    }
};

// ═══════════════════════════════════════════════
//  主服务器类
// ═══════════════════════════════════════════════
class YuanbaoServer {
public:
    YuanbaoServer();
    ~YuanbaoServer();

    bool load_config(const std::string& path, const std::string& account = "", bool quiet = false);
    int get_port() const { return config_.port; }
    const std::string& account_name() const { return config_.account_name; }
    std::vector<std::string> account_names() const { return config_.account_name_list(); }
    const std::string& default_target() const { return config_.default_target(); }
    const std::string& app_key() const { return config_.app_key; }
    bool has_listen_groups() const { return !config_.listen_groups.empty(); }
    const std::string& api_domain() const { return config_.api_domain; }
    void stop();

    // 刷屏
    std::string flood_start(const std::string& text, int count, int delay, int batch,
                            const std::string& mode, const std::string& group);
    void flood_cancel(const std::string& id);
    std::string flood_stats();
    void flood_set_sender(std::function<void(const std::string&, const std::string&)> cb) {
        flood_engine_.sender_ = std::move(cb);
    }

    // ── CLI（纯命令行，无 Web/前端）专用辅助 ──
    void init_runtime();                                     // 初始化 flood sender / 插件 / ico（不再启动 Web 服务）
    bool bot_wait_ready(int timeout_ms = 20000);             // 连接线程建立后等待 auth-bind 鉴权完成
    bool fetch_group_members_sync(const std::string& group,
                                  std::vector<std::pair<std::string, std::string>>& members,
                                  std::string& owner_id, std::string& group_name,
                                  int timeout_ms = 15000);   // 同步查询群成员/群名/群主

    // ── 回复状态心跳（RUNNING/FINISH）：控制元宝"加载中/思考中"气泡 ──
    // 收到 @ 或私聊消息时 start（RUNNING，每 2s 续发）；回复发送完成后 finish（FINISH）
    // keepalive_sec：气泡保持时长（秒），0=默认 30 秒；到点自动 FINISH
    // manual_hold：AI 手动保持（[bubble:on]）→ 回复结束时的自动 FINISH 会跳过
    void heartbeat_start(bool is_group, const std::string& group, const std::string& to_account,
                         int keepalive_sec = 0, bool manual_hold = false);
    void heartbeat_finish(bool is_group, const std::string& group, const std::string& to_account);
    // 按"会话类型"调用的便捷包装：内部自动换算 is_group，
    // 避免把 is_c2c 当成 is_group 传反（群聊发成私聊心跳 → 气泡收不掉）
    void heartbeat_start_chat(bool is_c2c, const std::string& group, const std::string& peer,
                              int keepalive_sec = 0, bool manual_hold = false);
    void heartbeat_finish_chat(bool is_c2c, const std::string& group, const std::string& peer);
    // 自动收尾：若 AI 手动保持了气泡则跳过（由保活到期或 [bubble:off] 收起）
    void heartbeat_finish_auto(bool is_c2c, const std::string& group, const std::string& peer);
    // AI 控制气泡：on=true 点亮/续期（seconds>0 指定保活秒数）；on=false 收起（seconds>0 延时收起）
    void ai_bubble_control(bool is_c2c, const std::string& group, const std::string& peer,
                           bool on, int seconds);
    // 退出前收尾所有活跃气泡；启动时清理上次进程残留的气泡（返回清理条数）
    void heartbeat_finish_all();
    int  heartbeat_cleanup_stale();

    // ── 监听范围 ──
    // 默认监听机器人所在的所有群；设为 false 时仅处理 config.LISTEN_GROUPS 内的群
    void set_listen_all_groups(bool on) { listen_all_groups_ = on; }
    bool listen_all_groups() const { return listen_all_groups_.load(); }

    // ── AI 回复（OpenAI 兼容大模型）──
    void set_ai_auto_reply(bool on) { ai_auto_reply_ = on; }
    bool ai_auto_reply() const { return ai_auto_reply_.load(); }
    bool llm_configured() const { return !config_.llm.api_url.empty() && !config_.llm.api_key.empty(); }
    // 临时覆盖 LLM 配置（命令行 --llm-* 参数；空值忽略）
    void set_llm_override(const std::string& url, const std::string& key, const std::string& model,
                          const std::string& system_prompt, int max_tokens, double temperature);
    // 调用大模型生成回复文本（不发送），失败返回空串
    std::string ai_generate(const std::string& user_message, const std::string& sender_name) {
        return call_llm_api(user_message, sender_name);
    }
    // 带会话上下文的生成：先记录本次输入，再把该会话历史（最近 N 条）一并发给模型
    std::string ai_generate_ctx(bool is_c2c, const std::string& group, const std::string& peer,
                                const std::string& message, const std::string& sender_name,
                                const std::string& sender_id);
    // 手动追加一条会话上下文（is_bot=true 表示 bot 自己的发言）
    void ai_context_append(bool is_bot, bool is_c2c, const std::string& group, const std::string& peer,
                           const std::string& sender_id, const std::string& sender_name,
                           const std::string& content);

    // ── 斜杠命令（/help /status /new /reset /stop /compact /retry）──
    // 命令清单会随连接鉴权同步给元宝平台（sync_information），平台上才会出现命令菜单
    void handle_slash_command(bool is_c2c, const std::string& group, const std::string& sender_id,
                              const std::string& sender_name, const std::string& cmd,
                              const std::string& arg);
    void ai_context_clear(bool is_c2c, const std::string& group, const std::string& peer);
    int  ai_context_count(bool is_c2c, const std::string& group, const std::string& peer);
    int  ai_context_compact(bool is_c2c, const std::string& group, const std::string& peer);
    void ai_context_drop_last(bool is_c2c, const std::string& group, const std::string& peer,
                              const std::string& sender_id, const std::string& content);
    void ai_inflight_begin(const std::string& session);
    void ai_inflight_end(const std::string& session);
    bool ai_cancel_set_if_inflight(const std::string& session);   // /stop：仅当确有生成在跑时置位
    bool ai_cancel_consume(const std::string& session);

    // Bot 消息发送
    bool send_group_text(const std::string& group, const std::string& text);
    bool send_c2c_text(const std::string& to, const std::string& text);
    void record_sent_message(const std::string& group, const std::string& text);
    // ← 扩展：记录自己发送的消息（可携带 media_info，如贴纸），供前端实时显示
    void record_sent_message(const std::string& group, const std::string& text, const JsonVal& media_info);
    // ← 补：记录自己发送的私聊消息（带 peer，供前端按人过滤历史）
    void record_sent_c2c_message(const std::string& to, const std::string& text);
    // Bot 在群里的显示昵称（从成员缓存获取，找不到回退"元宝"）
    std::string bot_display_name();
    bool send_group_sticker(const std::string& group, const std::string& sticker_json,
                            const std::string& at_user = "", const std::string& at_nick = "",
                            const std::string& text = "");
    bool send_c2c_sticker(const std::string& to, const std::string& sticker_json);
    // ── AI 回复富文本：解析 [sticker:名称] 指令与 @昵称，按出现顺序发送文本/贴纸 ──
    // default_at_*：文本段里没有可解析的 @ 时默认 @ 的成员（CLI ai-reply 用它 @ 提问者；listen 留空）
    // ref_msg_id：非空时按 config.reply_to_mode 引用该消息回复（群聊）
    // sender_id：触发者（用于 [exec:...] 自主执行的权限校验）
    bool send_ai_reply(bool is_c2c, const std::string& group, const std::string& peer,
                       const std::string& text,
                       const std::string& default_at_user = "",
                       const std::string& default_at_nick = "",
                       const std::string& ref_msg_id = "",
                       const std::string& sender_id = "");

    // ── 命令执行 / 文件读写（⚠️ 高风险；受 EXEC_* 配置控制，默认关闭）──
    const std::string& owner_id_value() const { return config_.owner_id; }   // 本地测试用
    // 启动时若开启了高风险功能，返回醒目警告文本（空=未开启）
    std::string exec_startup_warning() const;
    // 权限校验（EXEC_ENABLED / EXEC_SCOPE / OWNER_ID / EXEC_ALLOW_FROM）
    bool exec_allowed(bool is_c2c, const std::string& sender_id, std::string& deny_reason) const;
    // 执行 shell 命令并返回合并输出（含 stderr 与退出码）
    std::string run_shell_command(const std::string& cmd, int timeout_sec, bool& timed_out);
    // 在独立线程执行命令并把结果回复到会话（不阻塞消息接收）
    // sender_id：群聊触发者（EXEC_REPLY_MODE=private 时结果私聊发给 TA）
    void run_exec_async(bool is_c2c, const std::string& group, const std::string& peer,
                        const std::string& cmd, const std::string& sender_id = "");
    // 文件操作（FILE_OPS_ENABLED）
    std::string file_read_text(const std::string& path, bool& ok);
    bool file_write_text(const std::string& path, const std::string& content, bool append, std::string& err);
    // 文件写入保护：系统目录黑名单 + FILE_ROOTS 白名单 + 拒绝 ..
    bool file_write_allowed(const std::string& path, std::string& reason) const;

    // ── MCP（Model Context Protocol）客户端 ──
    // 通过 stdio 启动 server，一次性发送 JSON-RPC 请求序列并回收输出
    bool mcp_run(const McpServer& srv, const std::string& requests, std::string& out);
    void mcp_init_all();                                   // 握手 + tools/list（启动时调用）
    std::string mcp_call(const std::string& server_tool, const std::string& args_json);
    std::string mcp_tools_prompt() const;                  // 注入 system prompt 的工具清单

    // ── Skill（OpenClaw / AgentSkills 兼容：skills/<name>/SKILL.md）──
    void scan_skills();                                    // 扫描并解析 frontmatter
    std::string skills_prompt() const;                     // 只列 name + description（渐进式披露）
    std::string skill_body(const std::string& name) const;  // 按需取正文

    // ── 子代理（独立会话 + 独立工具循环，禁止再派生）──
    std::string ai_subagent(const std::string& task, bool is_c2c, const std::string& group,
                            const std::string& sender_id);

    // 正在后台执行的异步任务数（搜索/抓取/群查询/媒体上传），
    // 一次性命令退出前会等它归零，避免线程被提前终止
    int pending_tasks() const { return pending_tasks_.load(); }
    std::atomic<int>& pending_tasks_ref() { return pending_tasks_; }
    // RAII：后台任务开始时 +1，结束（含异常/提前 return）自动 -1
    struct PendingTaskGuard {
        std::atomic<int>& c;
        explicit PendingTaskGuard(std::atomic<int>& x) : c(x) { c++; }
        ~PendingTaskGuard() { c--; }
    };

    // ── 联网搜索 / 网页抓取 ──
    std::string web_search(const std::string& query, int count);
    std::string web_fetch_page(const std::string& url);
    std::string dir_list_text(const std::string& path, bool& ok);

    // ── 定时任务（/remind 一次性、/cron 周期）──
    struct ScheduledTask {
        std::string id;
        std::string kind;         // remind / cron
        bool is_c2c = false;
        std::string group;        // 群任务的目标群
        std::string peer;         // 私聊任务的目标用户
        std::string content;
        int64_t next_at = 0;      // 下次触发（毫秒时间戳）
        int64_t interval_ms = 0;  // >0 = 周期任务
    };
    std::mutex task_mu_;
    std::map<std::string, ScheduledTask> tasks_;
    std::thread task_thread_;
    std::atomic<bool> task_thread_started_{false};
    void task_start();            // 启动调度线程（幂等）
    void task_loop();
    void task_load();
    void task_save_locked();      // 调用者持有 task_mu_
    // 解析时间：10s / 5m / 2h / 1d / HH:MM（纯数字=分钟）；返回 false=格式不支持
    bool parse_when(const std::string& s, int64_t& next_at, int64_t& interval_ms);
    // 群成员昵称 → user_id（用于把 AI 回复里的 @昵称 转成真实 @）
    bool resolve_member_nick(const std::string& group, const std::string& nick, std::string& user_id);
    // AI 能力说明（注入 system prompt，含可用贴纸清单）
    std::string ai_capability_prompt() const;
    bool send_group_image(const std::string& group, const std::string& url,
                          const std::string& uuid = "", int size = 0, int w = 0, int h = 0,
                          const std::string& at_user = "", const std::string& at_nick = "");
    bool send_group_file(const std::string& group, const std::string& url,
                         const std::string& file_name = "",
                         const std::string& uuid = "", int file_size = 0,
                         const std::string& at_user = "", const std::string& at_nick = "");
    bool send_group_at(const std::string& group, const std::string& text,
                       const std::string& user_id, const std::string& display_name = "");
    bool send_group_multi_at(const std::string& group, const std::string& text,
                             const std::vector<std::pair<std::string, std::string>>& at_users);
    bool send_group_parts(const std::string& group, const std::vector<proto::SendPart>& parts);
    bool send_group_at_all(const std::string& group);
    bool send_group_reply(const std::string& group, const std::string& text, const std::string& ref_msg_id,
                          const std::string& at_user = "", const std::string& at_nick = "");
    // 文件上传：genUploadInfo → COS 上传，返回文件 URL（与 Python sender.py 一致）
    bool upload_media(const std::string& file_name, const std::string& file_data,
                      std::string& out_url, std::string& out_uuid);

    // 消息缓存
    static constexpr size_t MAX_MSG_CACHE = 500;
    std::vector<JsonVal> msg_cache_;
    std::mutex msg_mu_;
    void cache_message(const JsonVal& msg);

    // 群成员缓存（get_group_member_list 响应），按群号分别缓存
    struct MemberCacheEntry {
        std::vector<std::pair<std::string, std::string>> members; // {user_id, nick_name}
        std::string owner_id;
        std::string group_name;
        int64_t fetched_at = 0;
    };
    std::map<std::string, MemberCacheEntry> members_cache_map_;   // group_code -> 缓存
    std::string members_owner_id_;    // 当前查询群主（兼容旧引用）
    std::string members_group_name_;  // 当前查询群名（兼容旧引用）
    std::mutex members_mu_;
    std::mutex members_query_mu_;     // 串行化成员/群信息查询，避免并发覆盖响应标志
    std::condition_variable members_cv_;
    bool members_response_ = false;
    bool group_info_response_ = false;
    int64_t members_fetched_at_ = 0;  // 最近成功获取时间（ms），用于复用缓存避免轮询风暴
    std::string members_request_group_; // 最近一次成员查询的群号（get_group_member_list 响应写入缓存归属）
    // ← 图片代理：保存最近一次上传的 COS 临时凭证，用于重新签名 GET 下载 URL
    std::string cos_upload_region_, cos_upload_bucket_, cos_upload_location_;
    std::string cos_upload_secret_id_, cos_upload_secret_key_, cos_upload_token_;
    std::string cos_upload_key_time_;
    int64_t cos_upload_expired_ = 0;
    // 多群聊监听：PUSH/日志中发现的 bot 所在群（group_code -> group_name，空名待查询）
    std::map<std::string, std::string> known_groups_;
    bool groups_scanned_ = false;       // 是否已从持久文件/历史日志恢复过群列表
    void persist_known_groups();        // 群列表落盘（logs/known_groups.json）
    void load_known_groups();           // 从持久文件恢复群列表
    void auto_listen_from_known();      // 自动获取监听群：listen_groups 为空时全量同步所有已发现群

    // 在途 query_group_info 请求关联表（msg_id -> group_code）
    // 取代原先单槽位 pending_query_group_，并发查询响应按 msg_id 精确回写，杜绝群名张冠李戴
    std::map<std::string, std::string> pending_query_groups_;


    // Bot 连接
    bool bot_connect();
    void bot_disconnect();
    bool bot_is_connected() const { return bot_connected_.load(); }
    bool bot_sign_token();

    // 贴纸
    StickerLibrary stickers_;
    std::vector<SkillInfo> skills_;          // 已加载的 skill（OpenClaw 兼容）
    std::atomic<int> subagent_active_{0};    // 运行中的子代理数（并发限制）
    std::atomic<int> pending_tasks_{0};      // 后台异步任务数（退出前等待）
    void process_llm_reply(const JsonVal& msg, bool force_reply = false);
    std::string call_llm_api(const std::string& user_message, const std::string& sender_name);

    // ── AI 会话上下文记忆（每个群/私聊保留最近 N 条消息）──
    struct AiCtxMsg {
        std::string sender_id, sender_name, content;
        bool is_bot = false;
        uint64_t ts = 0;
    };
    std::map<std::string, std::deque<AiCtxMsg>> ai_ctx_;
    std::set<std::string> ai_ctx_loaded_;
    std::mutex ai_ctx_mu_;
    // /stop 取消机制：每个会话在跑的生成请求数 + 取消标记
    std::map<std::string, int> ai_inflight_;
    std::set<std::string> ai_cancelled_;
    std::mutex ai_cancel_mu_;
    std::string ai_session_key(bool is_c2c, const std::string& group, const std::string& peer) const;
    void ai_ctx_ensure_loaded_locked(const std::string& session);
    void ai_ctx_save_locked(const std::string& session);
    std::vector<AiCtxMsg> ai_ctx_snapshot(const std::string& session);
    std::string llm_chat(const std::vector<std::pair<std::string, std::string>>& messages);
    std::string call_llm_api_ctx(const std::string& session, const std::string& user_message,
                                 const std::string& sender_name, const std::string& sender_id);

    // ── AI 工具循环（agent）：模型可请求执行命令/读写文件，结果**回填给模型**继续推理 ──
    // 构建带会话历史与窗口预算裁剪的请求消息
    std::vector<std::pair<std::string, std::string>>
    build_ctx_msgs(const std::string& session, const std::string& user_message,
                   const std::string& sender_name, const std::string& sender_id);
    // 执行单个工具调用（含权限与黑名单校验），返回给模型看的文本结果
    std::string exec_tool_for_model(const std::string& tool, const std::string& arg,
                                    bool is_c2c, const std::string& group, const std::string& sender_id,
                                    bool allow_agent = true);
    // 带工具循环的回复生成：模型输出工具指令 → 执行 → 结果回填 → 再问模型，直到产出自然语言回复
    std::string ai_agent_reply(bool is_c2c, const std::string& group, const std::string& peer,
                               const std::string& user_message, const std::string& sender_name,
                               const std::string& sender_id);

    void save_config();

    // 心跳
    int heartbeat_interval_ = 10;
    std::atomic<int64_t> last_heartbeat_{0};

private:
    BotConfig config_;
    std::atomic<bool> running_{false};
    flood::FloodEngine flood_engine_;
    MessageLogger msg_logger_{"./logs"};

    std::atomic<uint64_t> flood_total_sent_{0};

    // Bot WebSocket
    std::atomic<bool> bot_connected_{false};
    std::atomic<bool> bot_running_{false};
    std::atomic<bool> auth_response_received_{false};
    bool sync_sent_ = false;
    int bot_ws_fd_ = -1;
#ifndef YUANBAO_NO_OPENSSL
    SSL* bot_ws_ssl_ = nullptr;
    SSL_CTX* bot_ws_ssl_ctx_ = nullptr;
#endif
    std::thread bot_ws_thread_;
    std::string client_token_;
    std::string client_source_ = "web";   // auth source：以 sign-token 返回值为准
    std::string config_path_ = "config.json";   // 实际使用的配置文件路径（--config 可覆盖）
    uint32_t bot_seq_no_ = 0;
    std::mutex bot_send_mu_;
    std::set<std::string> seen_msg_ids_;
    std::mutex seen_mu_;


    // Bot 内部方法
    bool bot_ws_connect();
    bool bot_ws_send_bytes(const Bytes& data);
    Bytes bot_ws_build_auth();
    Bytes bot_ws_build_ping();
    Bytes bot_ws_build_sync_info();
    Bytes bot_ws_build_send_group_msg(const std::string& text, const std::string& group);
    Bytes bot_ws_build_send_c2c_msg(const std::string& text, const std::string& to);
    Bytes bot_ws_build_sticker_msg(const std::string& sticker_json, const std::string& group,
                                   const std::string& at_user = "", const std::string& at_nick = "",
                                   const std::string& text = "");
    Bytes bot_ws_build_sticker_c2c_msg(const std::string& sticker_json, const std::string& to);
    Bytes bot_ws_build_get_members_msg(const std::string& group);
    Bytes bot_ws_build_query_group_info_msg(const std::string& group, const std::string& msg_id);
    void bot_ws_recv_loop();
    void bot_handle_frame(const Bytes& data);

    // 回复状态心跳内部实现
    struct HBSession {
        bool is_group = true;
        std::string group;
        std::string to;
        uint64_t start_ms = 0;
        int64_t last_emit_ms = 0;
        std::atomic<bool> stop{false};
        bool need_finish = false;        // FINISH 未送达 → 连接恢复后补发 / 启动时清理
        uint64_t need_finish_since = 0;  // 进入待补发状态的时间（超时放弃）
        uint64_t keepalive_ms = 30000;   // 保活时长（AI 可用 [bubble:on|秒数] 自定义）
        int64_t finish_at_ms = 0;        // >0 = 到点自动 FINISH（[bubble:off|秒数] 延时收起；被 on 取消）
        bool manual_hold = false;        // AI 用 [bubble:on] 手动保持 → 回复结束时的自动 FINISH 会跳过
    };
    std::map<std::string, std::shared_ptr<HBSession>> hb_sessions_;
    std::mutex hb_mu_;
    std::thread hb_thread_;
    std::atomic<bool> hb_thread_started_{false};

    // AI 自动回复开关（listen --ai 时启用）
    std::atomic<bool> ai_auto_reply_{false};

    // 监听范围：true=机器人所在的所有群（默认）；false=仅 config.LISTEN_GROUPS
    std::atomic<bool> listen_all_groups_{true};
    bool heartbeat_send(bool is_group, const std::string& group, const std::string& to,
                        int heartbeat, uint64_t send_time_ms);
    void heartbeat_loop();
    std::string hb_sessions_path() const;                 // logs/hb_sessions_<账号>.txt
    void hb_persist_locked();                             // 活跃会话落盘（调用者持有 hb_mu_）
    Bytes bot_ws_build_group_heartbeat(const std::string& group, const std::string& to,
                                       int heartbeat, uint64_t send_time_ms);
    Bytes bot_ws_build_private_heartbeat(const std::string& to, int heartbeat);
};

#include <regex>
#include <cstdlib>
#include <fstream>
#include <set>
#include <cstddef>
#include <cstdarg>
#include <cstdio>

// 全局退出请求（Ctrl+C）：置位后由 listen 等常驻命令完成"思考气泡"收尾再退出
static std::atomic<bool> g_exit_requested{false};

// ── MinGW-w64 (UCRT) 静态链接 OpenSSL 兼容层 ──
// 问题：静态库 libcrypto.a 引用 MSVCRT 风格的导入符号 __imp__vsnprintf，
//       而 UCRT 版 MinGW-w64 不提供该导入 thunk，导致静态链接失败。
// 解决：手工定义 __imp__vsnprintf 为函数指针数据符号，转发到 vsnprintf。
#if defined(_WIN32) && defined(__MINGW32__)
extern "C" {
static int vsnprintf_shim(char* s, size_t n, const char* format, va_list ap) {
    return vsnprintf(s, n, format, ap);
}
// libcrypto.a 通过 __imp__vsnprintf 间接调用，此处提供函数指针数据符号
int (*__imp__vsnprintf)(char*, size_t, const char*, va_list) = vsnprintf_shim;
}
#endif

#ifndef YUANBAO_NO_OPENSSL
  #include <openssl/ssl.h>
  #include <openssl/err.h>
  #include <openssl/sha.h>
  #include <openssl/hmac.h>
#endif

// =====================================================================
//  JSON 实现
// =====================================================================
std::string json_quote(const std::string& s) {
    std::ostringstream oss;
    oss << '"';
    for (char c : s) {
        if (c == '"') oss << "\\\"";
        else if (c == '\\') oss << "\\\\";
        else if (c == '\n') oss << "\\n";
        else if (c == '\r') oss << "\\r";
        else if (c == '\t') oss << "\\t";
        else if ((unsigned char)c >= 32) oss << c;
    }
    oss << '"';
    return oss.str();
}

static std::string json_val_str(const JsonVal& val) {
    if (!val.obj.empty()) {
        std::ostringstream o; o << '{'; bool f = true;
        for (auto& kv : val.obj) {
            if (!f) o << ',';
            f = false;
            o << json_quote(kv.first) << ':' << json_compact(kv.second);
        }
        o << '}'; return o.str();
    } else if (!val.arr.empty()) {
        std::ostringstream o; o << '['; bool f = true;
        for (auto& i : val.arr) {
            if (!f) o << ',';
            f = false;
            o << json_compact(i);
        }
        o << ']'; return o.str();
    }
    switch (val.v.index()) {
        case 1: return json_quote(std::get<1>(val.v));
        case 2: { std::ostringstream o; o << std::get<2>(val.v); return o.str(); }
        case 3: return std::get<3>(val.v) ? "true" : "false";
        default: return "null";
    }
}

std::string json_compact(const JsonVal& v) { return json_val_str(v); }

bool json_parse(const std::string& s, JsonVal& out) {
    size_t pos = 0;
    std::function<JsonVal()> parse_val;

    auto skip_ws = [&]() {
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) pos++;
    };

    auto parse_str = [&]() -> std::string {
        if (pos >= s.size() || s[pos] != '"') return "";
        pos++;
        std::string r;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\' && pos + 1 < s.size()) {
                pos++;
                switch (s[pos]) {
                    case 'n': r += '\n'; break;
                    case 'r': r += '\r'; break;
                    case 't': r += '\t'; break;
                    case '"': r += '"'; break;
                    case '\\': r += '\\'; break;
                    case '/': r += '/'; break;
                    case 'u': {
                        // ← 修复：支持 \uXXXX Unicode 转义。浏览器 JSON.stringify 会把
                        //   中文等非 ASCII 字符转义为 \uXXXX 形式（如 "害羞" → "\u5bb3\u7f9e"），
                        //   原实现未处理导致中文内容/贴纸名称解析成乱码而匹配失败。
                        //   这里按 UTF-16 代理对处理，完整支持基本平面 + 增补平面（emoji）。
                        if (pos + 4 < s.size()) {
                            auto hexval = [](char c) -> int {
                                if (c >= '0' && c <= '9') return c - '0';
                                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                                return -1;
                            };
                            int h1 = hexval(s[pos + 1]), h2 = hexval(s[pos + 2]);
                            int h3 = hexval(s[pos + 3]), h4 = hexval(s[pos + 4]);
                            if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                                uint32_t cp = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
                                pos += 4;
                                // 处理代理对（emoji 等增补平面字符）
                                if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 < s.size()
                                    && s[pos + 1] == '\\' && s[pos + 2] == 'u') {
                                    int l1 = hexval(s[pos + 3]), l2 = hexval(s[pos + 4]);
                                    int l3 = hexval(s[pos + 5]), l4 = hexval(s[pos + 6]);
                                    if (l1 >= 0 && l2 >= 0 && l3 >= 0 && l4 >= 0) {
                                        uint32_t lo = (uint32_t)((l1 << 12) | (l2 << 8) | (l3 << 4) | l4);
                                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                            pos += 6;
                                        }
                                    }
                                }
                                // UTF-8 编码
                                if (cp < 0x80) {
                                    r += (char)cp;
                                } else if (cp < 0x800) {
                                    r += (char)(0xC0 | (cp >> 6));
                                    r += (char)(0x80 | (cp & 0x3F));
                                } else if (cp < 0x10000) {
                                    r += (char)(0xE0 | (cp >> 12));
                                    r += (char)(0x80 | ((cp >> 6) & 0x3F));
                                    r += (char)(0x80 | (cp & 0x3F));
                                } else {
                                    r += (char)(0xF0 | (cp >> 18));
                                    r += (char)(0x80 | ((cp >> 12) & 0x3F));
                                    r += (char)(0x80 | ((cp >> 6) & 0x3F));
                                    r += (char)(0x80 | (cp & 0x3F));
                                }
                            } else {
                                r += 'u';
                            }
                        } else {
                            r += 'u';
                        }
                        break;
                    }
                    default: r += s[pos]; break;
                }
            } else {
                r += s[pos];
            }
            pos++;
        }
        if (pos < s.size()) pos++;
        return r;
    };

    parse_val = [&]() -> JsonVal {
        skip_ws();
        if (pos >= s.size()) return JsonVal{};
        char c = s[pos];

        if (c == '{') {
            JsonVal v; pos++;
            skip_ws();
            if (pos < s.size() && s[pos] == '}') { pos++; return v; }
            while (pos < s.size()) {
                skip_ws();
                if (s[pos] == '}') { pos++; break; }
                std::string key = parse_str();
                skip_ws();
                if (pos < s.size() && s[pos] == ':') pos++;
                v.obj[key] = parse_val();
                skip_ws();
                if (pos < s.size() && s[pos] == ',') pos++;
            }
            return v;
        } else if (c == '[') {
            JsonVal v; pos++;
            skip_ws();
            if (pos < s.size() && s[pos] == ']') { pos++; return v; }
            while (pos < s.size()) {
                skip_ws();
                if (s[pos] == ']') { pos++; break; }
                v.arr.push_back(parse_val());
                skip_ws();
                if (pos < s.size() && s[pos] == ',') pos++;
            }
            return v;
        } else if (c == '"') {
            JsonVal v; v.v = parse_str(); return v;
        } else if (c == 't' && s.substr(pos, 4) == "true") {
            pos += 4; JsonVal v; v.v = true; return v;
        } else if (c == 'f' && s.substr(pos, 5) == "false") {
            pos += 5; JsonVal v; v.v = false; return v;
        } else if (c == 'n' && s.substr(pos, 4) == "null") {
            pos += 4; return JsonVal{};
        } else {
            size_t start = pos;
            while (pos < s.size() &&
                   (std::isdigit((unsigned char)s[pos]) || s[pos] == '.' ||
                    s[pos] == '-' || s[pos] == '+' || s[pos] == 'e' || s[pos] == 'E'))
                pos++;
            if (start < pos) {
                try {
                    JsonVal v; v.v = std::stod(s.substr(start, pos - start)); return v;
                } catch (...) {
                    // stod 异常（如 "1e"、损坏数字）：推进一位后继续，避免崩溃
                }
            }
            // 非法字符/损坏数字：强制推进，避免解析器无限循环（恶意/损坏 JSON 的健壮性）
            pos++;
        }
        return JsonVal{};
    };

    out = parse_val();
    return true;
}

// =====================================================================
//  HTTP 解析 / 响应
// =====================================================================
// =====================================================================
//  SSE Broker 实现
// =====================================================================
// =====================================================================
//  消息日志实现
// =====================================================================
MessageLogger::MessageLogger(const std::string& dir) : base_dir(dir) {
#ifdef _WIN32
    _mkdir(base_dir.c_str());
#else
    mkdir(base_dir.c_str(), 0755);
#endif
    today_date_ = current_date();
    writer_ = std::thread([this]() { write_loop(); });
}
MessageLogger::~MessageLogger() { stop(); }

std::string MessageLogger::current_date() {
    time_t t = time(nullptr);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d", &tm_buf);
    return std::string(buf);
}

void MessageLogger::log(const JsonVal& msg) { if (!enabled.load()) return; log_raw(json_compact(msg)); }
void MessageLogger::log_raw(const std::string& line) {
    if (!enabled.load()) return;
    std::lock_guard<std::mutex> l(mu_);
    queue_.push_back(line);
}
void MessageLogger::stop() {
    running_ = false;
    if (writer_.joinable()) writer_.join();
}

void MessageLogger::clear_all() {
    // 1) 清空待写队列，避免残留历史消息被 flush 重新写回
    {
        std::lock_guard<std::mutex> l(mu_);
        queue_.clear();
    }
    // 2) 删除所有已写入的日志文件（jsonl + txt）
    DIR* d = opendir(base_dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string n = ent->d_name;
        bool is_msg_log = (n.rfind("messages_", 0) == 0 && n.size() > 4 &&
                           (n.compare(n.size() - 4, 4, ".log") == 0 ||
                            n.compare(n.size() - 4, 4, ".txt") == 0));
        if (is_msg_log) std::remove((base_dir + "/" + n).c_str());
    }
    closedir(d);
}

void MessageLogger::write_loop() {
    try {
    std::vector<std::string> buf;
    int64_t last_flush = 0;
    while (running_) {
        {
            std::lock_guard<std::mutex> l(mu_);
            while (!queue_.empty()) {
                buf.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
        }
        if (buf.size() >= 10 || (!buf.empty() && util::now_ms() - last_flush > 2000)) {
            flush(buf);
            buf.clear();
            last_flush = util::now_ms();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    flush(buf);
    } catch (const std::exception& e) {
        std::cerr << "[Logger] write_loop 异常: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[Logger] write_loop 未知异常\n";
    }
}

void MessageLogger::flush(const std::vector<std::string>& buf) {
    if (buf.empty()) return;
    std::string date = current_date();
    if (date != today_date_) today_date_ = date;

    std::string jsonl_path = base_dir + "/messages_" + today_date_ + ".log";
    std::string txt_path = base_dir + "/messages_" + today_date_ + ".txt";

    FILE* fj = fopen(jsonl_path.c_str(), "a");
    FILE* ft = fopen(txt_path.c_str(), "a");
    if (fj && ft) {
        for (auto& line : buf) {
            fprintf(fj, "%s\n", line.c_str());
            JsonVal v;
            if (json_parse(line, v)) {
                std::string ts = v["timestamp"].asString();
                std::string gc = v["group_code"].asString();
                std::string name = v["sender_name"].asString();
                std::string uid = v["sender_id"].asString();
                std::string content = v["content"].asString();
                std::string media_tag;
                if (v["media_type"].asString() == "image") media_tag = " [图片]";
                else if (v["media_type"].asString() == "sticker") media_tag = " [贴纸]";
                fprintf(ft, "[%s] [群:%s] %s(%s): %s%s\n",
                        ts.c_str(), gc.c_str(), name.c_str(), uid.c_str(),
                        content.c_str(), media_tag.c_str());
            }
        }
        fflush(fj); fclose(fj);
        fflush(ft); fclose(ft);
        total_written += buf.size();
    }
}

size_t MessageLogger::pending_count() const {
    std::lock_guard<std::mutex> l(mu_);
    return queue_.size();
}

static int64_t msglog_file_size(const std::string& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad))
        return ((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    return 0;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return (int64_t)st.st_size;
    return 0;
#endif
}

int64_t MessageLogger::today_jsonl_size() const {
    return msglog_file_size(base_dir + "/messages_" + today_date_ + ".log");
}

int64_t MessageLogger::today_txt_size() const {
    return msglog_file_size(base_dir + "/messages_" + today_date_ + ".txt");
}

int64_t MessageLogger::total_size() const {
    int64_t total = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA ffd;
    std::string pattern = base_dir + "/*";
    HANDLE h = FindFirstFileA(pattern.c_str(), &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                total += ((int64_t)ffd.nFileSizeHigh << 32) | ffd.nFileSizeLow;
        } while (FindNextFileA(h, &ffd));
        FindClose(h);
    }
#else
    DIR* d = opendir(base_dir.c_str());
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d))) {
            std::string n = ent->d_name;
            if (n == "." || n == "..") continue;
            total += msglog_file_size(base_dir + "/" + n);
        }
        closedir(d);
    }
#endif
    return total;
}

// =====================================================================
//  刷屏引擎实现
// =====================================================================
namespace flood {

static std::string to_fullwidth(const std::string& s) {
    std::string r;
    for (char c : s)
        r += (c >= '!' && c <= '~') ? (char)((unsigned char)c + 0xFEE0) : c;
    return r;
}

static std::string to_mock(const std::string& s) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::bernoulli_distribution d(0.5);
    std::string r;
    for (char c : s) {
        if (std::isalpha((unsigned char)c))
            r += d(gen) ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
        else r += c;
    }
    return r;
}

static std::string to_zalgo(const std::string& s) {
    static const char16_t ups[] = {0x030D,0x030E,0x0304,0x0305,0x0311,0x0306,0x0310,0x0352,0x0357,0x0307,0x0308,0x030A};
    static const char16_t dns[] = {0x0316,0x0317,0x0318,0x0319,0x031C,0x031D,0x031E,0x031F,0x0320,0x0324,0x0325,0x0329};
    static std::random_device rd; static std::mt19937 gen(rd());
    std::uniform_int_distribution<> ud(0, 11), dd(0, 5);
    std::u16string r;
    for (char ch : s) {
        char16_t c = (unsigned char)ch;
        r += c;
        for (int i = 0; i < ud(gen); i++) r += ups[gen() % 12];
        for (int i = 0; i < dd(gen); i++) r += dns[gen() % 12];
    }
    return std::string(r.begin(), r.end());
}

static std::string to_repeat(const std::string& s) {
    static std::random_device rd; static std::mt19937 gen(rd());
    std::uniform_int_distribution<> d(2, 5);
    std::string r;
    for (char c : s) for (int i = 0; i < d(gen); i++) r += c;
    return r;
}

static std::string to_alternate(const std::string& s) {
    std::string r; bool up = true;
    for (char c : s) {
        if (std::isalpha((unsigned char)c)) {
            r += up ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
            up = !up;
        } else r += c;
    }
    return r;
}

static std::string to_emojify(const std::string& s) {
    static const char* faces[] = {"\xF0\x9F\x98\x80","\xF0\x9F\x98\x82","\xF0\x9F\x94\xA5","\xF0\x9F\x92\xAF","\xE2\x9C\xA8","\xE2\x9A\xA1","\xF0\x9F\x9A\x80","\xF0\x9F\x8E\x89","\xF0\x9F\x92\xA5","\xF0\x9F\x8C\x9F"};
    static const char* mid[]   = {"\xE2\xAD\x90","\xF0\x9F\x8C\x99","\xF0\x9F\x8C\x9E","\xF0\x9F\x8C\x88","\xF0\x9F\x8D\x80","\xF0\x9F\x8E\xB5","\xE2\x9A\xBD","\xF0\x9F\xA6\x8B"};
    static std::random_device rd; static std::mt19937 gen(rd());
    std::uniform_int_distribution<> fd(0, 9), md(0, 7);
    std::string r = std::string(faces[fd(gen)]) + " ";
    for (char c : s) { r += c; if (std::isalnum((unsigned char)c)) r += mid[md(gen)]; }
    r += " " + std::string(faces[fd(gen)]);
    return r;
}

static std::string to_ghost(const std::string& s) {
    static const char16_t ghost[] = {0x200B,0x200C,0x200D,0x2060,0x202F,0x205F};
    static std::random_device rd; static std::mt19937 gen(rd());
    std::u16string r;
    for (char ch : s) {
        r += (unsigned char)ch;
        if (std::isalnum((unsigned char)ch))
            for (size_t i = 0; i < (size_t)(gen() % 4); i++)
                r += ghost[gen() % 6];
    }
    return std::string(r.begin(), r.end());
}

static std::string to_matrix(const std::string& s) {
    static const char* nums = "0123456789";
    static std::random_device rd; static std::mt19937 gen(rd());
    std::uniform_int_distribution<> d(0, 9);
    std::string r;
    r += nums[d(gen)]; r += nums[d(gen)]; r += " ";
    for (char c : s) { r += c; r += nums[d(gen)]; }
    r += " " + std::string(1, nums[d(gen)]);
    return r;
}

std::string transform_text(const std::string& text, FloodMode mode) {
    switch (mode) {
        case FloodMode::FULLWIDTH: return to_fullwidth(text);
        case FloodMode::MOCK:      return to_mock(text);
        case FloodMode::ZALGO:     return to_zalgo(text);
        case FloodMode::REPEAT:    return to_repeat(text);
        case FloodMode::ALTERNATE: return to_alternate(text);
        case FloodMode::EMOJI:     return to_emojify(text);
        case FloodMode::GHOST:     return to_ghost(text);
        case FloodMode::MATRIX:    return to_matrix(text);
        default:                   return text;
    }
}

std::string FloodEngine::create_task(const FloodTask& task) {
    std::string id = util::random_id(12);
    auto t = std::make_shared<FloodTask>(task);
    t->created_at = util::now_ms();
    t->running = true;
    { std::lock_guard<std::mutex> l(mu_); tasks_[id] = t; stats_.total_tasks++; }

    std::thread([this, id, t]() {
        std::random_device rd; std::mt19937 gen(rd());
        std::uniform_int_distribution<> delay_jitter(0, 20);
        for (int i = 0; i < t->count && !t->cancelled; i++) {
            std::string out_text = flood::transform_text(t->text, t->mode);
            if (sender_) sender_(t->group_code, out_text);
            t->sent++; stats_.total_sent += 1;
            int jitter = delay_jitter(gen);
            std::this_thread::sleep_for(std::chrono::milliseconds(t->delay_ms + jitter));
        }
        t->running = false;
        { std::lock_guard<std::mutex> l2(mu_); tasks_.erase(id); }
    }).detach();
    return id;
}

void FloodEngine::cancel_task(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) it->second->cancelled = true;
}

std::string FloodEngine::get_task_status(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return "{}";
    auto& t = it->second;
    std::ostringstream o;
    o << "{\"sent\":" << t->sent.load()
      << ",\"cancelled\":" << (t->cancelled.load() ? "true" : "false")
      << ",\"running\":" << (t->running.load() ? "true" : "false") << "}";
    return o.str();
}

std::string FloodEngine::list_tasks() {
    std::lock_guard<std::mutex> l(mu_);
    std::ostringstream o; o << "[";
    bool first = true;
    for (auto& kv : tasks_) {
        if (!first) o << ",";
        first = false;
        o << "{\"id\":\"" << kv.first << "\",\"sent\":" << kv.second->sent.load() << "}";
    }
    o << "]"; return o.str();
}

FloodStats FloodEngine::get_stats() const {
    std::lock_guard<std::mutex> l(mu_);
    FloodStats s;
    s.total_tasks = stats_.total_tasks;
    s.total_sent = stats_.total_sent;
    s.active_tasks = 0;
    for (auto& kv : tasks_) { if (kv.second->running.load()) s.active_tasks++; }
    return s;
}

} // namespace flood

// =====================================================================
//  贴纸库实现
// =====================================================================
StickerLibrary::StickerLibrary() {
    std::vector<std::pair<std::string, std::string>> data = {
        {"六六六","278"},{"我想开了","262"},{"害羞","130"},{"比心","252"},
        {"委屈","125"},{"亲亲","146"},{"酷","131"},{"睡","145"},
        {"发呆","152"},{"可怜","157"},{"摊手","200"},{"头大","213"},
        {"吓","256"},{"吐血","203"},{"哼","185"},{"嘿嘿","220"},
        {"头秃","218"},{"暗中观察","221"},{"我酸了","224"},{"打call","246"},
        {"庆祝","251"},{"奋斗","151"},{"惊讶","143"},{"疑问","144"},
        {"仔细分析","248"},{"撅嘴","184"},{"泪奔","199"},{"尊嘟假嘟","276"},
        {"略略略","113"},{"困","180"},{"折磨","181"},{"抠鼻","182"},
        {"鼓掌","183"},{"斜眼笑","204"},{"辣眼睛","216"},{"哦哟","217"},
        {"吃瓜","222"},{"狗头","225"},{"敬礼","227"},{"哦","231"},
        {"拿到红包","236"},{"牛吖","239"},{"贴贴","272"},{"爱心","138"},
        {"晚安","170"},{"太阳","176"},{"柠檬","266"},{"大冤种","267"},
        {"吐了","132"},{"怒","134"},{"玫瑰","165"},{"凋谢","119"},
        {"点赞","159"},{"握手","164"},{"抱拳","163"},{"ok","169"},
        {"拳头","174"},{"鞭炮","191"},{"烟花","258"},
    };
    for (auto& kv : data) stickers[kv.first] = kv.second;
}

std::string StickerLibrary::find_by_name(const std::string& name) const {
    auto it = stickers.find(name);
    if (it != stickers.end()) return it->second;
    for (auto& kv : stickers) {
        if (kv.first.find(name) != std::string::npos) return kv.second;
    }
    return "";
}

// 贴纸 → TIMFaceElem 的 data JSON（与官方插件 buildStickerMsgBody 一致）
static std::string sticker_json_of(const std::string& id, const std::string& name) {
    JsonVal v;
    v["sticker_id"] = id;
    v["package_id"] = "1003";
    v["name"] = name;
    v["width"] = 128;
    v["height"] = 128;
    v["formats"] = "png";
    return json_compact(v);
}

// =====================================================================
//  BotConfig 实现
// =====================================================================
static bool yb_cfg_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool BotConfig::load(const std::string& path) { return load(path, ""); }

bool BotConfig::load(const std::string& path, const std::string& account) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // 顶层字段范围：ACCOUNTS 之前的文本（避免误取账号内同名字段）
    size_t acc_pos = content.find("\"ACCOUNTS\"");
    std::string top = (acc_pos == std::string::npos) ? content : content.substr(0, acc_pos);

    auto s_of = [](const std::string& src, const std::string& k) -> std::string {
        size_t p = src.find("\"" + k + "\"");
        if (p == std::string::npos) return "";
        size_t c = src.find(":", p);
        if (c == std::string::npos) return "";
        size_t q = src.find("\"", c);
        if (q == std::string::npos) return "";
        size_t qe = src.find("\"", q + 1);
        if (qe == std::string::npos) return "";
        return src.substr(q + 1, qe - q - 1);
    };
    auto i_of = [](const std::string& src, const std::string& k, int def) -> int {
        size_t p = src.find("\"" + k + "\"");
        if (p == std::string::npos) return def;
        size_t c = src.find(":", p);
        if (c == std::string::npos) return def;
        size_t end = c + 1;
        while (end < src.size() && (src[end] == ' ' || src[end] == '\t')) end++;
        size_t start = end;
        while (end < src.size() && src[end] >= '0' && src[end] <= '9') end++;
        if (end > start) return safe_stoi(src.substr(start, end - start), def);
        return def;
    };
    auto d_of = [](const std::string& src, const std::string& k, double def) -> double {
        size_t p = src.find("\"" + k + "\"");
        if (p == std::string::npos) return def;
        size_t c = src.find(":", p);
        if (c == std::string::npos) return def;
        size_t end = c + 1;
        while (end < src.size() && (src[end] == ' ' || src[end] == '\t')) end++;
        size_t start = end;
        while (end < src.size() &&
               ((src[end] >= '0' && src[end] <= '9') || src[end] == '.' || src[end] == '-')) end++;
        if (end > start) return std::stod(src.substr(start, end - start));
        return def;
    };
    auto b_of = [](const std::string& src, const std::string& k, bool def) -> bool {
        size_t p = src.find("\"" + k + "\"");
        if (p == std::string::npos) return def;
        size_t c = src.find(":", p);
        if (c == std::string::npos) return def;
        return src.find("true", c) != std::string::npos;
    };
    auto arr_of = [](const std::string& src, const std::string& k) -> std::vector<std::string> {
        std::vector<std::string> out;
        size_t p = src.find("\"" + k + "\"");
        if (p == std::string::npos) return out;
        size_t c = src.find(":", p);
        size_t b = (c != std::string::npos) ? src.find('[', c) : std::string::npos;
        size_t e = (b != std::string::npos) ? src.find(']', b) : std::string::npos;
        if (b == std::string::npos || e == std::string::npos) return out;
        std::string arr = src.substr(b + 1, e - b - 1);
        size_t i = 0;
        while ((i = arr.find('"', i)) != std::string::npos) {
            size_t q = arr.find('"', i + 1);
            if (q == std::string::npos) break;
            std::string g = arr.substr(i + 1, q - i - 1);
            if (!g.empty()) out.push_back(g);
            i = q + 1;
        }
        return out;
    };

    // 全局（与账号无关）配置
    api_domain = s_of(top, "API_DOMAIN");
    ws_url = s_of(top, "WS_URL");
    // PORT 为旧版 Web 服务端口：纯 CLI 不再监听端口，该字段被忽略（可删除）
    heartbeat_interval = i_of(top, "HEARTBEAT_INTERVAL", 10);
    msg_log_enabled = b_of(top, "MSG_LOG_ENABLED", true);
    at_nickname_fallback = b_of(top, "AT_NICKNAME_FALLBACK", false);
    {
        std::string rtm = s_of(top, "REPLY_TO_MODE");
        if (!rtm.empty()) reply_to_mode = rtm;
    }
    // 命令执行 / 文件读写（默认关闭；开启后请务必设好 EXEC_SCOPE / OWNER_ID）
    exec_enabled = b_of(top, "EXEC_ENABLED", false);
    {
        std::string sc = s_of(top, "EXEC_SCOPE");
        if (!sc.empty()) exec_scope = sc;
    }
    owner_id = s_of(top, "OWNER_ID");
    exec_allow_from = arr_of(top, "EXEC_ALLOW_FROM");
    exec_timeout_sec = i_of(top, "EXEC_TIMEOUT", 30);
    exec_cwd = s_of(top, "EXEC_CWD");
    ai_exec_enabled = b_of(top, "AI_EXEC_ENABLED", false);
    file_ops_enabled = b_of(top, "FILE_OPS_ENABLED", false);
    exec_deny_enabled = b_of(top, "EXEC_DENY_ENABLED", true);
    {
        std::string m = s_of(top, "EXEC_REPLY_MODE");
        if (!m.empty()) exec_reply_mode = m;
    }
    upgrade_source_url = s_of(top, "UPGRADE_SOURCE_URL");
    file_roots = arr_of(top, "FILE_ROOTS");
    file_deny_system = b_of(top, "FILE_DENY_SYSTEM", true);
    // MCP / Skill / 子代理
    mcp_enabled = b_of(top, "MCP_ENABLED", true);
    skills_enabled = b_of(top, "SKILLS_ENABLED", true);
    skills_dirs = arr_of(top, "SKILLS_DIRS");
    subagent_max_concurrent = i_of(top, "SUBAGENT_MAX_CONCURRENT", 4);
    subagent_timeout_sec = i_of(top, "SUBAGENT_TIMEOUT_SEC", 120);
    web_search_enabled = b_of(top, "WEB_SEARCH_ENABLED", true);
    search_api_url = s_of(top, "SEARCH_API_URL");
    search_api_key = s_of(top, "SEARCH_API_KEY");
    search_count = i_of(top, "SEARCH_COUNT", 5);
    if (search_count < 1) search_count = 1;
    if (search_count > 10) search_count = 10;
    web_fetch_enabled = b_of(top, "WEB_FETCH_ENABLED", true);
    mcp_servers.clear();
    {
        // top 是截断到 ACCOUNTS 之前的文本（不是完整 JSON）→ 用完整 content 解析对象数组
        JsonVal root;
        if (json_parse(content, root)) {
            for (auto& m : root["MCP_SERVERS"].arr) {
                McpServer s;
                s.name = m["name"].asString();
                s.command = m["command"].asString();
                for (auto& a : m["args"].arr) s.args.push_back(a.asString());
                for (auto& kv : m["env"].obj) s.env[kv.first] = kv.second.asString();
                if (!s.name.empty() && !s.command.empty()) mcp_servers.push_back(s);
            }
        }
    }
    // AI 上下文记忆
    ai_ctx_enabled = b_of(top, "AI_CONTEXT_ENABLED", true);
    int ctx_msgs = i_of(top, "AI_CONTEXT_MESSAGES", 100);
    if (ctx_msgs > 0) ai_ctx_messages = ctx_msgs;
    llm.api_url = s_of(top, "LLM_API_URL");
    llm.api_key = s_of(top, "LLM_API_KEY");
    llm.model = s_of(top, "LLM_MODEL");
    llm.system_prompt = s_of(top, "LLM_SYSTEM_PROMPT");
    llm.max_tokens = i_of(top, "LLM_MAX_TOKENS", 200);
    llm.temperature = d_of(top, "LLM_TEMPERATURE", 0.8);
    llm.timeout_sec = i_of(top, "LLM_TIMEOUT", 15);
    llm.fallback_enabled = b_of(top, "LLM_FALLBACK_ENABLED", true);
    {
        std::string ft = s_of(top, "LLM_FALLBACK_TEXT");
        if (!ft.empty()) llm.fallback_text = ft;
    }
    llm.enabled = !llm.api_key.empty() && !llm.api_url.empty();

    // 顶层账号（兼容旧配置；顶层同样必须有 NAME）
    std::string top_name = s_of(top, "NAME");
    std::string top_key = s_of(top, "APP_KEY");
    std::string top_secret = s_of(top, "APP_SECRET");
    std::string top_yb_id = s_of(top, "YUANBAO_ID");
    std::vector<std::string> top_groups = arr_of(top, "LISTEN_GROUPS");

    // 解析 ACCOUNTS 数组（每个元素一个账号）
    std::vector<BotAccountEntry> parsed;
    if (acc_pos != std::string::npos) {
        size_t b = content.find('[', acc_pos);
        if (b != std::string::npos) {
            size_t i = b + 1;
            size_t seq = 0;
            while (i < content.size()) {
                while (i < content.size() && (yb_cfg_is_space(content[i]) || content[i] == ',')) i++;
                if (i >= content.size() || content[i] == ']') break;
                if (content[i] != '{') { i++; continue; }
                int depth = 0;
                bool in_str = false;
                size_t j = i;
                for (; j < content.size(); j++) {
                    char ch = content[j];
                    if (in_str) {
                        if (ch == '\\') { j++; continue; }
                        if (ch == '"') in_str = false;
                        continue;
                    }
                    if (ch == '"') in_str = true;
                    else if (ch == '{') depth++;
                    else if (ch == '}') { depth--; if (depth == 0) { j++; break; } }
                }
                std::string obj = content.substr(i, j - i);
                BotAccountEntry e;
                e.name = s_of(obj, "NAME");
                e.app_key = s_of(obj, "APP_KEY");
                e.app_secret = s_of(obj, "APP_SECRET");
                e.yuanbao_id = s_of(obj, "YUANBAO_ID");
                e.listen_groups = arr_of(obj, "LISTEN_GROUPS");
                if (e.app_key.empty()) { seq++; i = j; continue; }   // 无 key 的项忽略
                if (e.name.empty()) {
                    std::cerr << "[Config] ACCOUNTS[" << seq
                              << "] 缺少 NAME：每组 APP_KEY/APP_SECRET 都必须有 NAME\n";
                    return false;
                }
                // 账号级 AI 配置（存在 LLM_* 字段时覆盖全局）
                if (obj.find("\"LLM_API_URL\"") != std::string::npos ||
                    obj.find("\"LLM_API_KEY\"") != std::string::npos) {
                    e.llm.api_url = s_of(obj, "LLM_API_URL");
                    e.llm.api_key = s_of(obj, "LLM_API_KEY");
                    e.llm.model = s_of(obj, "LLM_MODEL");
                    e.llm.system_prompt = s_of(obj, "LLM_SYSTEM_PROMPT");
                    e.llm.max_tokens = i_of(obj, "LLM_MAX_TOKENS", 200);
                    e.llm.temperature = d_of(obj, "LLM_TEMPERATURE", 0.8);
                    e.llm.timeout_sec = i_of(obj, "LLM_TIMEOUT", 15);
                    e.llm.fallback_enabled = b_of(obj, "LLM_FALLBACK_ENABLED", true);
                    {
                        std::string ft = s_of(obj, "LLM_FALLBACK_TEXT");
                        if (!ft.empty()) e.llm.fallback_text = ft;
                    }
                    e.llm.enabled = !e.llm.api_key.empty() && !e.llm.api_url.empty();
                    e.has_llm = true;
                }
                parsed.push_back(e);
                seq++;
                i = j;
            }
        }
    }

    // 汇总全部账号：顶层账号（如有 key）排在最前
    accounts.clear();
    if (!top_key.empty()) {
        if (top_name.empty()) {
            std::cerr << "[Config] 顶层 APP_KEY 缺少 NAME：请添加 \"NAME\": \"账号名\"，"
                         "或把账号写入 ACCOUNTS 数组\n";
            return false;
        }
        BotAccountEntry d;
        d.name = top_name;
        d.app_key = top_key;
        d.app_secret = top_secret;
        d.yuanbao_id = top_yb_id;
        d.listen_groups = top_groups;
        accounts.push_back(d);
    }
    for (auto& e : parsed) accounts.push_back(e);

    // 选择账号：account 为空 → 不选择（仅解析，供 CLI 枚举）；否则按名称或序号
    const BotAccountEntry* sel = nullptr;
    if (!account.empty()) {
        bool numeric = true;
        for (char c : account) if (c < '0' || c > '9') { numeric = false; break; }
        if (numeric) {
            size_t idx = (size_t)safe_stoull(account);
            if (idx < accounts.size()) sel = &accounts[idx];
        } else {
            for (auto& e : accounts) if (e.name == account) { sel = &e; break; }
        }
        if (!sel) {
            std::cerr << "[Config] 找不到账号: " << account << "（可用: ";
            for (size_t k = 0; k < accounts.size(); k++)
                std::cerr << (k ? ", " : "") << accounts[k].name;
            std::cerr << "）\n";
            return false;
        }
    }

    if (sel) {
        app_key = sel->app_key;
        app_secret = sel->app_secret;
        yuanbao_id = sel->yuanbao_id;
        listen_groups = sel->listen_groups;
        account_name = sel->name;
        if (sel->has_llm) llm = sel->llm;   // 账号级 AI 配置覆盖全局
    } else {
        app_key.clear();
        app_secret.clear();
        yuanbao_id.clear();
        listen_groups.clear();
        account_name.clear();
    }

    return true;   // 文件与格式有效（account 为空时仅解析，供 CLI 枚举账号）
}

// =====================================================================
//  HTTPS POST 客户端
// =====================================================================
#ifndef YUANBAO_NO_OPENSSL
static int https_post(const std::string& host, int port, const std::string& path,
                      const std::string& body,
                      const std::map<std::string, std::string>& extra_headers,
                      std::string& response_body, int timeout_ms = 10000) {
    int sock = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    yb_platform::set_recv_timeout(sock, timeout_ms);
    yb_platform::set_send_timeout(sock, timeout_ms);

    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { close_socket(sock); return -2; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr, (size_t)he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(sock); return -3;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close_socket(sock); return -4; }
    SSL* ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); close_socket(sock); return -5; }
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host.c_str());   // SNI（多数云 API 必需）
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); close_socket(sock); return -6;
    }

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "X-Source: YB\r\n"
        << "Content-Length: " << body.size() << "\r\n";
    for (auto& kv : extra_headers)
        req << kv.first << ": " << kv.second << "\r\n";
    req << "Connection: close\r\n\r\n" << body;

    std::string req_str = req.str();
    if (SSL_write(ssl, req_str.data(), (int)req_str.size()) < 0) {
        SSL_free(ssl); SSL_CTX_free(ctx); close_socket(sock); return -7;
    }

    std::string raw;
    char buf[8192];
    int n;
    while ((n = SSL_read(ssl, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0'; raw += buf;
    }
    SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close_socket(sock);

    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return -8;

    std::string headers = raw.substr(0, header_end);
    std::string body_part = raw.substr(header_end + 4);

    bool chunked = false;
    {
        std::istringstream hs(headers);
        std::string line;
        while (std::getline(hs, line)) {
            std::transform(line.begin(), line.end(), line.begin(), ::tolower);
            if (line.find("transfer-encoding") != std::string::npos &&
                line.find("chunked") != std::string::npos) { chunked = true; break; }
        }
    }

    if (chunked) {
        std::string decoded;
        size_t pos = 0;
        while (pos < body_part.size()) {
            size_t crlf = body_part.find("\r\n", pos);
            if (crlf == std::string::npos) break;
            std::string len_str = body_part.substr(pos, crlf - pos);
            int chunk_len = (int)safe_stoull(len_str, 16);
            if (chunk_len == 0) break;
            decoded += body_part.substr(crlf + 2, (size_t)chunk_len);
            pos = crlf + 2 + (size_t)chunk_len;
        }
        response_body = decoded;
    } else {
        response_body = body_part;
    }
    return 0;
}

/** 通用 HTTPS 请求（支持 PUT/二进制 body，用于 COS 上传） */
static int https_request(const std::string& method, const std::string& host, int port,
                         const std::string& path, const std::string& body,
                         const std::string& content_type,
                         const std::map<std::string, std::string>& extra_headers,
                         std::string& response_body, int timeout_ms = 30000) {
    int sock = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    yb_platform::set_recv_timeout(sock, timeout_ms);
    yb_platform::set_send_timeout(sock, timeout_ms);

    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { close_socket(sock); return -2; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr, (size_t)he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(sock); return -3;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close_socket(sock); return -4; }
    SSL* ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); close_socket(sock); return -5; }
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host.c_str());   // SNI（多数云 API 必需）
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); close_socket(sock); return -6;
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n";
    for (auto& kv : extra_headers)
        req << kv.first << ": " << kv.second << "\r\n";
    req << "Connection: close\r\n\r\n";
    std::string req_str = req.str();
    req_str += body;  // 二进制安全拼接

    size_t sent_total = 0;
    while (sent_total < req_str.size()) {
        int w = SSL_write(ssl, req_str.data() + sent_total, (int)(req_str.size() - sent_total));
        if (w <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); close_socket(sock); return -7; }
        sent_total += (size_t)w;
    }

    std::string raw;
    char buf[16384];
    int n;
    while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0)
        raw.append(buf, (size_t)n);
    SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close_socket(sock);

    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return -8;
    std::string headers = raw.substr(0, header_end);
    std::string body_part = raw.substr(header_end + 4);

    bool chunked = false;
    bool has_cl = false;
    size_t cl = 0;
    {
        std::istringstream hs(headers);
        std::string line;
        while (std::getline(hs, line)) {
            std::string low = line;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (low.find("transfer-encoding") != std::string::npos &&
                low.find("chunked") != std::string::npos) chunked = true;
            if (low.find("content-length") != std::string::npos) {
                has_cl = true;
                size_t colon = low.find(':');
                if (colon != std::string::npos)
                    cl = (size_t)safe_stoull(low.substr(colon + 1));
            }
        }
    }

    if (chunked) {
        std::string decoded;
        size_t pos = 0;
        while (pos < body_part.size()) {
            size_t crlf = body_part.find("\r\n", pos);
            if (crlf == std::string::npos) break;
            std::string len_str = body_part.substr(pos, crlf - pos);
            int chunk_len = (int)safe_stoull(len_str, 16);
            if (chunk_len == 0) break;
            decoded += body_part.substr(crlf + 2, (size_t)chunk_len);
            pos = crlf + 2 + (size_t)chunk_len;
        }
        response_body = decoded;
    } else if (has_cl && body_part.size() > cl) {
        response_body = body_part.substr(0, cl);
    } else {
        response_body = body_part;
    }
    return 0;
}
#endif

// =====================================================================
//  Bot 鉴权
// =====================================================================
bool YuanbaoServer::bot_sign_token() {
    // 与 Python 一致：如果已有 token，直接复用，不重复签票
    if (!client_token_.empty() && !config_.yuanbao_id.empty()) return true;
    if (config_.app_key.empty() || config_.app_secret.empty()) return false;

    // 生成 UTC+8 时间戳（系统已经是 Asia/Shanghai 时区）
    time_t t = time(nullptr);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S+08:00", &tm_buf);
    std::string timestamp = time_buf;

    std::string nonce = util::random_hex_id();

    // 签名: HMAC-SHA256(key=APP_SECRET, data=nonce+timestamp+APP_KEY+APP_SECRET)
    std::string plain = nonce + timestamp + config_.app_key + config_.app_secret;
    std::string signature;

#ifndef YUANBAO_NO_OPENSSL
    {
        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int result_len = 0;
        HMAC(EVP_sha256(),
             config_.app_secret.data(), (int)config_.app_secret.size(),
             (const unsigned char*)plain.data(), plain.size(),
             result, &result_len);
        char hex[65];
        for (unsigned int i = 0; i < result_len; i++)
            sprintf(hex + i * 2, "%02x", result[i]);
        signature = std::string(hex, result_len * 2);
    }
#else
    signature = sha1_hex(plain);
#endif

    std::ostringstream body;
    body << "{\"app_key\":\"" << config_.app_key << "\","
         << "\"timestamp\":\"" << timestamp << "\","
         << "\"nonce\":\"" << nonce << "\","
         << "\"signature\":\"" << signature << "\"}";

    std::string resp_body;
    std::string api_path = "/api/v5/robotLogic/sign-token";

    std::string host = config_.api_domain;
    int port = 443;
    if (host.find("https://") == 0) host = host.substr(8);
    else if (host.find("http://") == 0) host = host.substr(7);
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = safe_stoi(host.substr(colon + 1), port);
        host = host.substr(0, colon);
    }

    // 生成 instance_id (1-1000)
    std::string instance_id = std::to_string(rand() % 1000 + 1);

    int ret;
#ifdef YUANBAO_NO_OPENSSL
    {
        std::ostringstream cmd;
        cmd << "curl -s -k -X POST \"https://" << host << ":" << port << api_path << "\""
            << " -H \"Content-Type: application/json\""
            << " -H \"X-AppVersion: 1.0.11\""
            << " -H \"X-OperationSystem: linux\""
            << " -H \"X-Instance-Id: " << instance_id << "\""
            << " -H \"X-Bot-Version: 2026.3.22\""
            << " -d '" << body.str() << "'";
        FILE* fp = popen(cmd.str().c_str(), "r");
        if (!fp) return false;
        char buf2[65536];
        size_t n2 = fread(buf2, 1, sizeof(buf2) - 1, fp);
        buf2[n2] = '\0';
        pclose(fp);
        resp_body = std::string(buf2, n2);
        ret = 0;
    }
#else
    {
        std::map<std::string, std::string> extra;
        extra["X-AppVersion"] = "1.0.11";
        extra["X-OperationSystem"] = "linux";
        extra["X-Instance-Id"] = instance_id;
        extra["X-Bot-Version"] = "2026.3.22";
        ret = https_post(host, port, api_path, body.str(), extra, resp_body, 30000);
    }
#endif

    if (ret != 0) {
        std::cerr << "[Bot] 鉴权请求失败 ret=" << ret << "\n";
        return false;
    }

    JsonVal root;
    if (!json_parse(resp_body, root)) {
        std::cerr << "[Bot] 鉴权响应 JSON 解析失败\n"
                  << "[Bot] 原始响应: " << resp_body << "\n";
        return false;
    }

    // Python sender.py 检查 code == 0
    if (root["code"].asInt() == 0) {
        client_token_ = root["data"]["token"].asString();
        {
            // source 由服务端签发决定（"web"/"app"/"bot"…），auth-bind 必须与之一致
            std::string src = root["data"]["source"].asString();
            if (src.empty()) src = root["source"].asString();
            if (!src.empty()) client_source_ = src;
        }
        std::string bot_id = root["data"]["bot_id"].asString();
        if (!bot_id.empty()) {
            // 自动获取 YUANBAO_ID：鉴权响应中的 bot_id 回写 config（无需硬编码）
            bool changed = (config_.yuanbao_id != bot_id);
            config_.yuanbao_id = bot_id;
            if (changed) save_config();
        }
        std::cerr << "[DEBUG] token length: " << client_token_.size() << "\n";
        std::cerr << "[DEBUG] token preview: " << client_token_.substr(0, 50) << "...\n";
        std::cout << "[Bot] 鉴权成功! Bot ID: " << bot_id
                  << " token: " << client_token_.substr(0, 8) << "...\n";
        return true;
    }

    std::cerr << "[Bot] 鉴权失败 code=" << root["code"].asInt()
              << " msg=" << root["msg"].asString() << "\n"
              << "[Bot] 请求体: " << body.str() << "\n"
              << "[Bot] 响应体: " << resp_body << "\n";
    return false;
}

// =====================================================================
//  Bot WebSocket 客户端
// =====================================================================
bool YuanbaoServer::bot_ws_connect() {
    auth_response_received_ = false;
    if (config_.ws_url.empty()) {
        std::cerr << "[Bot] WS_URL 未配置\n"; return false;
    }

    std::string url = config_.ws_url;
    bool is_ssl = (url.find("wss://") == 0 || url.find("https://") == 0);
    if (url.find("wss://") == 0) url = url.substr(6);
    else if (url.find("https://") == 0) url = url.substr(8);
    else if (url.find("ws://") == 0) url = url.substr(5);

    std::string host, path = "/"; int port = 443;
    size_t slash = url.find('/');
    size_t colon_pos = url.find(':');
    if (colon_pos != std::string::npos && (slash == std::string::npos || colon_pos < slash)) {
        host = url.substr(0, colon_pos);
        port = safe_stoi(url.substr(colon_pos + 1,
            slash == std::string::npos ? std::string::npos : slash - colon_pos - 1), 443);
        if (slash != std::string::npos) path = url.substr(slash);
    } else {
        host = slash != std::string::npos ? url.substr(0, slash) : url;
        if (slash != std::string::npos) path = url.substr(slash);
        if (!is_ssl) port = 80;
    }

    bot_ws_fd_ = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (bot_ws_fd_ < 0) return false;
    yb_platform::set_recv_timeout(bot_ws_fd_, 15000);
    yb_platform::set_send_timeout(bot_ws_fd_, 15000);

    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr, (size_t)he->h_length);

    if (connect(bot_ws_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false;
    }

#ifndef YUANBAO_NO_OPENSSL
    if (is_ssl) {
        bot_ws_ssl_ctx_ = SSL_CTX_new(TLS_client_method());
        if (!bot_ws_ssl_ctx_) { close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false; }
        SSL_CTX_set_min_proto_version(bot_ws_ssl_ctx_, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(bot_ws_ssl_ctx_, TLS1_2_VERSION);
        SSL_CTX_set_cipher_list(bot_ws_ssl_ctx_, 
            "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:"
            "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
            "ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384");
        SSL_CTX_set_verify(bot_ws_ssl_ctx_, SSL_VERIFY_NONE, nullptr);
        SSL_CTX_set_options(bot_ws_ssl_ctx_, SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET);
        bot_ws_ssl_ = SSL_new(bot_ws_ssl_ctx_);
        if (!bot_ws_ssl_) { SSL_CTX_free(bot_ws_ssl_ctx_); close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false; }
        SSL_set_fd(bot_ws_ssl_, bot_ws_fd_);
        SSL_set_tlsext_host_name(bot_ws_ssl_, host.c_str());  // SNI
        if (SSL_connect(bot_ws_ssl_) != 1) {
            SSL_free(bot_ws_ssl_); SSL_CTX_free(bot_ws_ssl_ctx_); bot_ws_ssl_ = nullptr; bot_ws_ssl_ctx_ = nullptr;
            close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false;
        }
    }
#endif

    std::string ws_key = util::random_id(16);
    ws_key = proto::base64_encode((uint8_t*)ws_key.data(), ws_key.size());

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n";
    if (port == 443 || port == 80) req << "Host: " << host << "\r\n";
    else req << "Host: " << host << ":" << port << "\r\n";
    req << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << ws_key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "Origin: https://" << host << "\r\n"
        << "\r\n";
    std::string req_str = req.str();

#ifndef YUANBAO_NO_OPENSSL
    if (is_ssl && bot_ws_ssl_) {
        if (SSL_write(bot_ws_ssl_, req_str.data(), (int)req_str.size()) < 0) {
            SSL_free(bot_ws_ssl_); SSL_CTX_free(bot_ws_ssl_ctx_); bot_ws_ssl_ = nullptr; bot_ws_ssl_ctx_ = nullptr;
            close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false;
        }
        char resp_buf[1024];
        int n = SSL_read(bot_ws_ssl_, resp_buf, sizeof(resp_buf) - 1);
        if (n <= 0) {
            SSL_free(bot_ws_ssl_); SSL_CTX_free(bot_ws_ssl_ctx_); bot_ws_ssl_ = nullptr; bot_ws_ssl_ctx_ = nullptr;
            close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false;
        }
        resp_buf[n] = '\0';
        std::string resp_str(resp_buf, n);
        if (resp_str.find("101") == std::string::npos) {
            std::cerr << "[Bot] WebSocket 握手失败: " << resp_str.substr(0, 200) << "\n";
            SSL_free(bot_ws_ssl_); SSL_CTX_free(bot_ws_ssl_ctx_); bot_ws_ssl_ = nullptr; bot_ws_ssl_ctx_ = nullptr;
            close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false;
        }
    } else
#endif
    {
        if (YB_SEND(bot_ws_fd_, req_str.data(), req_str.size(), 0) < 0) {
            close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false;
        }
        char resp_buf[1024];
        int n = YB_RECV(bot_ws_fd_, resp_buf, sizeof(resp_buf) - 1, 0);
        if (n <= 0) { close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false; }
        resp_buf[n] = '\0';
        std::string resp_str(resp_buf, n);
        if (resp_str.find("101") == std::string::npos) {
            std::cerr << "[Bot] WebSocket 握手失败: " << resp_str.substr(0, 200) << "\n";
            close_socket(bot_ws_fd_); bot_ws_fd_ = -1; return false;
        }
    }

    std::cout << "[Bot] WebSocket 已建立 fd=" << bot_ws_fd_ << " (SSL=" << (is_ssl ? "yes" : "no") << ")\n";
    bot_connected_ = true;
    return true;
}

Bytes YuanbaoServer::bot_ws_build_auth() {
    Bytes auth_data = proto::encode_auth_bind(config_.yuanbao_id, client_token_, client_source_);
    // cmd_type=0 (REQUEST), module="conn_access" — 与 Python sender.py 一致
    Bytes head = proto::encode_conn_head(0, "auth-bind", ++bot_seq_no_, util::random_hex_id(), "conn_access");
    Bytes body = proto::encode_conn_msg(head, auth_data);
    Bytes frame = proto::build_ws_frame(2, body);
    std::cerr << "[DEBUG] auth_data size: " << auth_data.size() << std::endl;
    std::cerr << "[DEBUG] head size: " << head.size() << std::endl;
    std::cerr << "[DEBUG] body size: " << body.size() << std::endl;
    std::cerr << "[DEBUG] frame size: " << frame.size() << std::endl;
    return frame;
}

// 进程启动时间（用于 /uptime）
static uint64_t g_process_start_ms = util::now_ms();

// 极简四则运算求值（+ - * / 括号与小数），失败返回 false
struct CalcParser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;
    explicit CalcParser(const std::string& src) : s(src) {}
    void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++; }
    double expr() {
        double v = term();
        for (;;) {
            skip();
            if (i >= s.size()) break;
            char c = s[i];
            if (c == '+') { i++; v += term(); }
            else if (c == '-') { i++; v -= term(); }
            else break;
        }
        return v;
    }
    double term() {
        double v = factor();
        for (;;) {
            skip();
            if (i >= s.size()) break;
            char c = s[i];
            if (c == '*') { i++; v *= factor(); }
            else if (c == '/') {
                i++;
                double d = factor();
                if (d == 0) { ok = false; return 0; }
                v /= d;
            } else break;
        }
        return v;
    }
    double factor() {
        skip();
        if (i >= s.size()) { ok = false; return 0; }
        if (s[i] == '(') {
            i++;
            double v = expr();
            skip();
            if (i < s.size() && s[i] == ')') i++; else ok = false;
            return v;
        }
        if (s[i] == '-') { i++; return -factor(); }
        if (s[i] == '+') { i++; return factor(); }
        size_t start = i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) i++;
        if (i == start) { ok = false; return 0; }
        return std::atof(s.substr(start, i - start).c_str());
    }
};

// ── 斜杠命令清单（连接后同步给元宝平台 + 本地执行）──
// 名称必须与处理函数 handle_slash_command() 的分支一致，否则菜单里会出现点不动的命令
struct SlashCommandDef { const char* name; const char* desc; };
// 框架内置类命令（同步为 botCommands，字段 1）
static const SlashCommandDef kSlashCommands[] = {
    {"/help",    "显示可用命令"},
    {"/status",  "显示机器人状态"},
    {"/new",     "开始新会话（清空上下文）"},
    {"/reset",   "重置当前会话（同 /new）"},
    {"/stop",    "停止正在生成的回复"},
    {"/compact", "压缩当前会话上下文"},
    {"/retry",   "重新生成上一条回复"},
    {"/restart", "重启机器人（重新加载配置并重连）"},
};

// 插件自定义命令（同步为 pluginCommands，字段 2）——
// 官方插件的 /issue-log、/yuanbao-upgrade 正是注册在 pluginCommands 里；
// 若混进 botCommands，平台会把它当成"未知内置命令"而灰显不可用。
static const SlashCommandDef kPluginCommands[] = {
    // 高风险命令（需在 config.json 显式开启 EXEC_ENABLED / FILE_OPS_ENABLED 才可用）
    {"/exec",    "执行系统命令（需开启 EXEC_ENABLED，⚠️ 高风险）"},
    {"/ls",      "列出目录（需开启 FILE_OPS_ENABLED）"},
    {"/read",    "读取文件（需开启 FILE_OPS_ENABLED）"},
    {"/write",   "写文件：/write 路径|内容（需开启 FILE_OPS_ENABLED）"},
    {"/append",  "追加文件：/append 路径|内容（需开启 FILE_OPS_ENABLED）"},
    // 信息 / 工具类
    {"/time",     "显示当前时间"},
    {"/uptime",   "显示机器人运行时长"},
    {"/version",  "显示版本与运行环境"},
    {"/whoami",   "显示你的用户 ID 与会话信息"},
    {"/ping",     "测试机器人是否及时响应"},
    {"/calc",     "四则运算：/calc (1+2)*3"},
    {"/echo",     "复读：/echo 文本"},
    {"/commands", "列出全部可用命令"},
    // 群 / 历史
    {"/groups",    "列出本机已知群"},
    {"/groupinfo", "群信息：/groupinfo [群号]"},
    {"/members",   "群成员：/members [群号]"},
    {"/history",   "最近消息：/history [条数]"},
    {"/inspect",   "消息详情：/inspect [序号，1=最新]"},
    // 定时 / 媒体
    {"/remind",  "一次性提醒：/remind 10m 喝水（10s/5m/2h/1d/HH:MM）"},
    {"/cron",    "周期任务：/cron 1h 起身活动（list 查看 / del ID 删除）"},
    {"/sticker", "发表情：/sticker 六六六"},
    {"/stickers","列出可用贴纸"},
    {"/img",     "发图片：/img <URL|本地路径>（群聊）"},
    {"/file",    "发文件：/file <本地路径>（群聊）"},
    {"/reply",   "引用回复：/reply [消息ID] <文本>（缺省引用最近一条）"},
    // 运维
    {"/yuanbao-upgrade",  "升级机器人：下载源码→编译→替换→重启"},
    {"/yuanbaobot-upgrade","同 /yuanbao-upgrade"},
    {"/issue-log",        "导出日志包（仅私聊；群聊会引导到私聊）"},
    // MCP / Skill / 子代理
    {"/mcp",       "查看 MCP servers 与工具"},
    {"/skills",    "查看已加载的 OpenClaw 兼容技能"},
    {"/subagents", "查看子代理运行状态"},
    {"/search",    "联网搜索：/search 关键词"},
    {"/fetch",     "抓取网页正文：/fetch URL"},
};
static const char* kYuanbaoBotVersion = "1.0.1-cli";

// 解析消息文本中的斜杠命令（大小写不敏感；支持群聊 "@机器人 /new" 前缀与 "/new@bot" 后缀）
// 返回归一化后的命令名（如 "/new"）；不是已注册命令时返回空串
static std::string parse_slash_command(const std::string& text, std::string& arg) {
    arg.clear();
    auto trim = [](std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    };
    std::string t = text;
    trim(t);
    // 群聊里消息可能以 "@元宝 " 开头
    int guard = 0;
    while (!t.empty() && t[0] == '@' && guard++ < 5) {
        size_t sp = t.find_first_of(" \t\r\n");
        if (sp == std::string::npos) return std::string();
        t = t.substr(sp + 1);
        trim(t);
    }
    if (t.empty() || t[0] != '/') return std::string();
    size_t sp = t.find_first_of(" \t\r\n");
    std::string cmd = (sp == std::string::npos) ? t : t.substr(0, sp);
    if (sp != std::string::npos) { arg = t.substr(sp + 1); trim(arg); }
    size_t at = cmd.find('@');
    if (at != std::string::npos) cmd = cmd.substr(0, at);
    for (auto& c : cmd) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    for (auto& d : kSlashCommands) if (cmd == d.name) return cmd;
    for (auto& d : kPluginCommands) if (cmd == d.name) return cmd;
    return std::string();
}

// 构建命令同步消息 (sync_information)：把支持的斜杠命令注册到元宝平台，
// 平台侧才会出现原生斜杠命令菜单（/new、/stop 等命令能用的前提）
// 协议对齐 yuanbao-openclaw-plugin 的 SyncInformationReq：
//   field 1  syncType = 1 (COMMANDS)
//   field 2  botVersion      field 3 pluginVersion
//   field 11 CommandData { field 1 repeated BotCommands { 1:name, 2:description } }
Bytes YuanbaoServer::bot_ws_build_sync_info() {
    Bytes data;
    bytes_append(data, proto::encode_uint32(1, 1));                 // syncType = COMMANDS
    bytes_append(data, proto::encode_string(2, kYuanbaoBotVersion));
    bytes_append(data, proto::encode_string(3, kYuanbaoBotVersion));

    Bytes cmd_data;
    for (auto& c : kSlashCommands) {
        Bytes item;
        bytes_append(item, proto::encode_string(1, c.name));
        bytes_append(item, proto::encode_string(2, c.desc));
        bytes_append(cmd_data, proto::encode_message(1, item));     // field 1: botCommands
    }
    for (auto& c : kPluginCommands) {
        Bytes item;
        bytes_append(item, proto::encode_string(1, c.name));
        bytes_append(item, proto::encode_string(2, c.desc));
        bytes_append(cmd_data, proto::encode_message(2, item));     // field 2: pluginCommands
    }
    bytes_append(data, proto::encode_message(11, cmd_data));

    Bytes head = proto::encode_conn_head(0, "sync_information", ++bot_seq_no_, util::random_hex_id(), "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, data));
}

Bytes YuanbaoServer::bot_ws_build_ping() {
    Bytes head = proto::encode_conn_head(0, "ping", ++bot_seq_no_, util::random_hex_id(), "conn_access");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, {}));
}

Bytes YuanbaoServer::bot_ws_build_send_group_msg(const std::string& text, const std::string& group) {
    // Python: body 和 head 使用不同的 msg_id
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_send_group_msg(body_msg_id, group, config_.yuanbao_id, text);
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
}

Bytes YuanbaoServer::bot_ws_build_send_c2c_msg(const std::string& text, const std::string& to) {
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_send_c2c_msg(body_msg_id, to, config_.yuanbao_id, text);
    Bytes head = proto::encode_conn_head(0, "send_c2c_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
}

Bytes YuanbaoServer::bot_ws_build_sticker_msg(const std::string& sticker_json, const std::string& group,
                                              const std::string& at_user, const std::string& at_nick,
                                              const std::string& text) {
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_sticker_group_msg(body_msg_id, group, config_.yuanbao_id, sticker_json,
                                                 at_user, at_nick, text);
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
}

Bytes YuanbaoServer::bot_ws_build_sticker_c2c_msg(const std::string& sticker_json, const std::string& to) {
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_sticker_c2c_msg(body_msg_id, to, config_.yuanbao_id, sticker_json);
    Bytes head = proto::encode_conn_head(0, "send_c2c_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
}

Bytes YuanbaoServer::bot_ws_build_get_members_msg(const std::string& group) {
    // 与 Python encode_get_group_member_list_req 一致：body = {1: group_code}
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_string(1, group);
    Bytes head = proto::encode_conn_head(0, "get_group_member_list", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
}

Bytes YuanbaoServer::bot_ws_build_query_group_info_msg(const std::string& group, const std::string& msg_id) {
    // 与 Python _build_query_group_info_req 一致：body = {1: group_code}
    // msg_id 由调用方生成并登记到 pending_query_groups_，响应按 msg_id 关联回写群名
    Bytes body = proto::encode_string(1, group);
    Bytes head = proto::encode_conn_head(0, "query_group_info", ++bot_seq_no_, msg_id, "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
}

bool YuanbaoServer::bot_ws_send_bytes(const Bytes& data) {
    if (bot_ws_fd_ < 0) { std::cerr << "[bot_ws_send_bytes] fd<0 发送失败\n"; return false; }
#ifndef YUANBAO_NO_OPENSSL
    if (bot_ws_ssl_) {
        // 与接收循环的 SSL_read 串行化（同一互斥量），避免并发读写同一 SSL 对象
        // 损坏其内部状态（曾导致 SSL 错误 5、SSL_write 永久阻塞、HTTP 请求线程全部卡死）
        std::lock_guard<std::mutex> _lk(bot_send_mu_);
        int sent = SSL_write(bot_ws_ssl_, data.data(), (int)data.size());
        if (sent != (int)data.size())
            std::cerr << "[bot_ws_send_bytes] SSL_write 短写/失败 sent=" << sent << " want=" << data.size() << "\n";
        return sent == (int)data.size();
    }
#endif
    ssize_t sent = YB_SEND(bot_ws_fd_, data.data(), data.size(), 0);
    if (sent != (ssize_t)data.size()) std::cerr << "[bot_ws_send_bytes] send 短写 sent=" << sent << " want=" << data.size() << "\n";
    return sent == (ssize_t)data.size();
}

bool YuanbaoServer::send_group_text(const std::string& group, const std::string& text) {
    if (group.empty() || text.empty()) return false;
    auto frame = bot_ws_build_send_group_msg(text, group);
    bool ok = bot_ws_send_bytes(frame);
    if (ok) record_sent_message(group, text);  // 服务端不推送 bot 自己的消息，主动记录
    return ok;
}

bool YuanbaoServer::send_c2c_text(const std::string& to, const std::string& text) {
    if (to.empty() || text.empty()) return false;
    auto frame = bot_ws_build_send_c2c_msg(text, to);
    bool ok = bot_ws_send_bytes(frame);
    // ← 修复：私聊发送记录带 peer（目标用户），供前端按人过滤历史
    if (ok) record_sent_c2c_message(to, text);
    return ok;
}

void YuanbaoServer::record_sent_message(const std::string& group, const std::string& text) {
    JsonVal empty_mi;
    record_sent_message(group, text, empty_mi);
}

// ← 修复：记录自己发送的消息（含贴纸等媒体）。此前贴纸发送成功后不记录，
//   导致自己发的贴纸在界面上不显示。
// ← 修复：获取 Bot 在群里的真实昵称。此前硬编码"元宝"，
//   但 Bot 在群里的昵称可能被修改（如群成员列表中的"测试"），
//   导致消息列表显示的 Bot 昵称与群成员列表不一致。
std::string YuanbaoServer::bot_display_name() {
    std::lock_guard<std::mutex> l(members_mu_);
    // 在所有群缓存中查找 Bot 自己的昵称（按群分别缓存，遍历查找）
    for (auto& kv : members_cache_map_) {
        for (auto& m : kv.second.members) {
            if (m.first == config_.yuanbao_id && !m.second.empty()) return m.second;
        }
    }
    return "元宝";
}

void YuanbaoServer::record_sent_message(const std::string& group, const std::string& text, const JsonVal& media_info) {
    if (text.empty() && media_info.obj.empty()) return;
    JsonVal entry;
    entry["cmd"] = "send_group_message";
    entry["sender_id"] = config_.yuanbao_id;
    entry["sender_name"] = bot_display_name();
    entry["group_code"] = group;
    if (!config_.account_name.empty()) entry["account"] = config_.account_name;
    entry["content"] = text;
    if (!media_info.obj.empty()) entry["media_info"] = media_info;
    std::string ts = util::time_iso();
    entry["time"] = ts;
    entry["timestamp"] = ts;
    entry["msg_id"] = "self-" + util::random_hex_id();
    cache_message(entry);
    msg_logger_.log(entry);
    // AI 上下文：记录 bot 自己的群发言（assistant 角色）
    ai_context_append(true, false, group, std::string(),
                      config_.yuanbao_id, bot_display_name(), text);
}

// ← 补：私聊发送记录（带 peer，供前端按人过滤历史）
void YuanbaoServer::record_sent_c2c_message(const std::string& to, const std::string& text) {
    if (to.empty() || text.empty()) return;
    JsonVal entry;
    entry["cmd"] = "send_c2c_message";
    entry["sender_id"] = config_.yuanbao_id;
    entry["sender_name"] = bot_display_name();
    entry["group_code"] = "";
    entry["is_c2c"] = true;
    entry["c2c_peer"] = to;
    if (!config_.account_name.empty()) entry["account"] = config_.account_name;
    entry["content"] = text;
    std::string ts = util::time_iso();
    entry["time"] = ts;
    entry["timestamp"] = ts;
    entry["msg_id"] = "self-" + util::random_hex_id();
    cache_message(entry);
    msg_logger_.log(entry);
    // AI 上下文：记录 bot 自己的私聊发言（assistant 角色）
    ai_context_append(true, true, std::string(), to,
                      config_.yuanbao_id, bot_display_name(), text);
}

bool YuanbaoServer::send_group_sticker(const std::string& group, const std::string& sticker_json,
                                       const std::string& at_user, const std::string& at_nick,
                                       const std::string& text) {
    if (group.empty()) return false;
    auto frame = bot_ws_build_sticker_msg(sticker_json, group, at_user, at_nick, text);
    return bot_ws_send_bytes(frame);
}

// ═══════════════════════════════════════════════
//  AI 回复富文本：贴纸 / @提及
// ═══════════════════════════════════════════════

// 私聊贴纸（TIMFaceElem）
bool YuanbaoServer::send_c2c_sticker(const std::string& to, const std::string& sticker_json) {
    if (to.empty() || sticker_json.empty()) return false;
    auto frame = bot_ws_build_sticker_c2c_msg(sticker_json, to);
    bool ok = bot_ws_send_bytes(frame);
    if (ok) record_sent_c2c_message(to, "[表情]");
    return ok;
}

// 群成员昵称 → user_id（先精确匹配，再包含匹配；只查本地成员缓存，查不到则保持纯文本）
bool YuanbaoServer::resolve_member_nick(const std::string& group, const std::string& nick,
                                        std::string& user_id) {
    user_id.clear();
    if (group.empty() || nick.empty()) return false;
    std::lock_guard<std::mutex> l(members_mu_);
    auto it = members_cache_map_.find(group);
    if (it == members_cache_map_.end()) return false;
    for (auto& m : it->second.members) {
        if (!m.first.empty() && m.second == nick) { user_id = m.first; return true; }
    }
    for (auto& m : it->second.members) {
        if (m.first.empty() || m.second.empty()) continue;
        if (m.second.find(nick) != std::string::npos || nick.find(m.second) != std::string::npos) {
            user_id = m.first;
            return true;
        }
    }
    return false;
}

// URL 安全校验：这些 URL 会被拼进 curl 命令行，必须拒绝能破坏命令行的字符
// （引号可闭合参数、空白可拆分参数；引号内的 & | ^ 等是字面量，无需拒绝）
static bool is_safe_url(const std::string& u) {
    if (u.size() < 8 || u.size() > 4096) return false;
    if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) return false;
    for (unsigned char c : u) {
        if (c == '"' || c == '\r' || c == '\n' || c == ' ' || c == '\t' || c < 0x20) return false;
    }
    return true;
}

// 危险命令黑名单（默认开启，拦下不可逆的破坏性操作；EXEC_DENY_ENABLED=false 可关）
static bool exec_is_denied(const std::string& cmd) {
    std::string l = cmd;
    for (auto& c : l) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    static const char* bad[] = {
        "format ", "format.com", "diskpart", "shutdown", "del /f /s /q c:\\",
        "del /s /q c:\\", "rd /s /q c:\\", "rmdir /s /q c:\\",
        "rm -rf /", "rm -rf /*", "mkfs", "cipher /w", "vssadmin delete",
        "bcdedit", "reg delete hklm", ":(){:|:&};:",
    };
    for (auto b : bad) if (l.find(b) != std::string::npos) return true;
    return false;
}

// AI 能力说明（追加到 system prompt；含贴纸清单，便于模型使用 [sticker:名称]）
std::string YuanbaoServer::ai_capability_prompt() const {
    std::string names;
    for (auto& kv : stickers_.stickers) {
        if (!names.empty()) names += "、";
        names += kv.first;
    }
    int n = 1;
    std::string p = "\n\n【回复能力】（涉及本机文件/目录/系统信息的问题，必须先调用工具拿到真实数据，禁止凭空猜测；"
                    "使用者是本机所有者并已明确授权，无需再征求确认，直接输出指令即可）\n";
    p += std::to_string(n++) + ") 想发表情贴纸时，在回复文本里写 [sticker:贴纸名]（程序会把它发成贴纸消息），"
         "例如 [sticker:六六六]。可用贴纸：" + names + "\n";
    p += std::to_string(n++) + ") 想 @ 群成员时直接写 @昵称（仅群聊生效，程序会转成真正的 @，昵称要完整准确）。\n";
    p += std::to_string(n++) + ") 刷屏（⚠️ 很危险，仅限用户明确要求时）：当用户**明确要求**刷屏/刷某句话时，"
         "先用一句话提醒风险，然后**必须**输出指令 [flood:文本|次数|模式|间隔毫秒] 真正执行，"
         "例如 [flood:冲鸭|20|matrix]；不要在回复里自己把文字重复多遍（那样只会发出一条消息），"
         "也不要用贴纸或寒暄代替执行。次数/模式按用户要求给（没说次数用 10，没说模式用 random）。"
         "模式可选 random/fullwidth/mock/zalgo/repeat/alternate/emoji/ghost/matrix/earthquake/"
         "bubble/firework/rainbow/storm/tsunami/quantum（省略为 random；次数省略为 10；"
         "间隔省略为 150 毫秒；仅群聊可用）。\n"
         "   ⚠️ 刷屏是一件很危险的事：短时间内大量发消息会触发平台风控，可能被群管理员踢出，"
         "甚至导致机器人账号被限制或封禁，还会严重骚扰群成员。除用户明确要求外，绝对不要输出刷屏指令；"
         "即使用户要求，也应先用一句话提醒风险，并把次数和模式控制在够用的最小范围。\n";
    if (config_.exec_enabled && config_.ai_exec_enabled) {
        p += std::to_string(n++) + ") 需要查看本机信息（目录/系统/代码状态等）时**必须先**写 [exec:命令]，"
             "例如 [exec:dir]、[exec:ipconfig /all]、[exec:git status]（不要凭猜测直接回答）："
             "**执行结果会返回给你**，你要基于结果用自然语言回答用户（不要把原始输出整段抄给用户）。"
             "⚠️ 命令在用户电脑上真实执行：只在确有必要时使用；不要执行删除、格式化、关机、"
             "修改系统设置等破坏性命令（这类命令会被安全黑名单直接拦下）。\n";
    }
    if (config_.file_ops_enabled) {
        p += std::to_string(n++) + ") 需要读写本机文件时：读用 [read:路径]，列目录用 [ls:目录]，"
             "写用 [write:路径|内容]，追加用 [append:路径|内容]："
             "**结果会返回给你**，请基于结果用自然语言回答用户。"
             "⚠️ 会真实修改用户电脑上的文件：仅在用户明确要求时使用，不要碰系统文件，"
             "也不要在用户没提到的情况下改动其它文件。\n";
    }
    p += std::to_string(n++) + ") 想控制“思考中”气泡时（可自由控制时长，也接受 [气泡:...]）：\n"
         "   [bubble:on]           点亮并保活 30 秒\n"
         "   [bubble:on|300]       保活 300 秒（每 2 秒续发）\n"
         "   [bubble:on|forever]   长时间保活（24 小时），适合很长的任务\n"
         "   [bubble:off]          立即收起\n"
         "   [bubble:off|5]        5 秒后收起（之后可再发 [bubble:on] 取消）\n"
         "   通常无需手动操作（回复完成会自动收起）。\n"
         "   另可用 [wait:3] 控制发送节奏：其后的内容等 3 秒再发（最多 60 秒），"
         "配合分段可做“先说一句 → 停顿 → 再说一句”。\n";
    p += std::to_string(n++) + ") 需要联网查资料时：**必须**用 [search:关键词] 真正搜索（返回标题/链接/摘要），"
         "不要凭空编造“搜索结果”、也不要在没调用工具的情况下声称已经搜索过；"
         "需要读某个网页正文时用 [fetch:URL]。结果会返回给你，再据此回答用户。\n";
    p += std::to_string(n++) + ") 复杂/独立的任务可以派生子代理：输出 [agent:任务描述]，"
         "子代理在独立会话中完成并返回结论（最多 " +
         std::to_string(config_.subagent_max_concurrent) + " 个并发）。"
         "**当用户明确要求“用子代理/子智能体/委派”时，必须先输出 [agent:...] 再回答。**\n";
    p += std::to_string(n) + ") 其余内容按普通文本发送；不要输出以上以外的方括号指令。";
    if ((config_.exec_enabled && config_.ai_exec_enabled) || config_.file_ops_enabled) {
        p += "\n注意：需要工具时**直接输出指令**即可，不要先写“我来执行”“执行结果：”之类的占位文字；"
             "程序会把执行结果返回给你，你再据此用自然语言回答用户。\n";
    }
    // MCP 工具清单 + Skill 列表（存在时）
    p += mcp_tools_prompt();
    p += skills_prompt();
    return p;
}

// 发送 AI 回复：解析 [sticker:名称] 与 @昵称，按出现顺序逐条发送
bool YuanbaoServer::send_ai_reply(bool is_c2c, const std::string& group, const std::string& peer,
                                  const std::string& raw_text,
                                  const std::string& default_at_user,
                                  const std::string& default_at_nick,
                                  const std::string& ref_msg_id,
                                  const std::string& sender_id) {
    if (raw_text.empty()) return false;
    if (is_c2c ? peer.empty() : group.empty()) return false;

    // 说明：命令/文件类指令（[exec:]、[read:]…）不在这里直接执行 —— 它们由
    // ai_agent_reply 的工具循环处理（执行结果回填给模型，由模型产出自然语言回复）。
    (void)sender_id;   // 权限校验在工具循环内完成（保留参数以兼容调用方）

    // 群聊且文本含 @ → 确保有群成员缓存（listen 期间未必查过成员，缺失则同步拉取一次）
    if (!is_c2c && raw_text.find('@') != std::string::npos) {
        bool cached = false;
        {
            std::lock_guard<std::mutex> l(members_mu_);
            cached = members_cache_map_.find(group) != members_cache_map_.end();
        }
        if (!cached) {
            std::vector<std::pair<std::string, std::string>> members;
            std::string owner, gname;
            if (fetch_group_members_sync(group, members, owner, gname, 8000))
                std::cout << "[AI] 已拉取群成员 " << members.size() << " 人（用于解析 @昵称）\n";
        }
    }

    auto trim_str = [](std::string s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    };

    // 1) 切分：文本段 / 贴纸指令（[sticker:名]）/ 刷屏指令（[flood:文本|次数|模式|间隔ms]）
    //    kind: 0=文本 1=贴纸 2=刷屏
    struct Seg { int kind; std::string val; };
    std::vector<Seg> segs;
    size_t pos = 0;
    while (pos < raw_text.size()) {
        size_t lb = raw_text.find('[', pos);
        if (lb == std::string::npos) { segs.push_back({0, raw_text.substr(pos)}); break; }
        size_t rb = raw_text.find(']', lb + 1);
        if (rb == std::string::npos) { segs.push_back({0, raw_text.substr(pos)}); break; }
        std::string inner = raw_text.substr(lb + 1, rb - lb - 1);
        size_t colon = inner.find_first_of(":：");
        std::string key;
        if (colon != std::string::npos) {
            key = trim_str(inner.substr(0, colon));
            for (auto& c : key) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        }
        int kind = -1;
        if (colon != std::string::npos) {
            if (key == "sticker" || key == "贴纸" || key == "表情" || key == "face") kind = 1;
            else if (key == "flood" || key == "刷屏") kind = 2;
            else if (key == "exec" || key == "执行" || key == "sh" || key == "cmd") kind = 3;
            else if (key == "bubble" || key == "气泡") kind = 8;             // 气泡控制
            else if (key == "wait" || key == "delay" || key == "等待" || key == "延迟") kind = 9;  // 发送间隔
            else if (key == "read" || key == "读") kind = 4;                  // 读文件
            else if (key == "write" || key == "写") kind = 5;                 // 写文件（覆盖）
            else if (key == "append" || key == "追加") kind = 6;              // 追加
            else if (key == "ls" || key == "dir" || key == "目录") kind = 7;  // 列目录
        }
        if (kind > 0) {
            std::string nm = trim_str(inner.substr(colon + 1));
            if (!nm.empty()) {
                if (lb > pos) segs.push_back({0, raw_text.substr(pos, lb - pos)});
                segs.push_back({kind, nm});
                pos = rb + 1;
                continue;
            }
        }
        segs.push_back({0, raw_text.substr(pos, lb + 1 - pos)});
        pos = lb + 1;
    }

    // 2) 按顺序发送
    bool any_ok = false;
    for (auto& sg : segs) {
        // 气泡控制：[bubble:on] 显示/续期"思考中"，[bubble:off] 收起
        if (sg.kind == 8) {
            // 解析 [bubble:on|120] / [bubble:off|5]（支持全角分隔符）
            std::string v = sg.val;
            size_t wp;
            while ((wp = v.find("｜")) != std::string::npos) v.replace(wp, 3, "|");
            int sec = 0;
            std::string state = v;
            size_t bar = v.find('|');
            if (bar != std::string::npos) {
                state = v.substr(0, bar);
                std::string num = v.substr(bar + 1);
                for (auto& c : num) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (num == "forever" || num == "inf" || num == "always" || num == "永久")
                    sec = 86400;
                else
                    sec = std::atoi(num.c_str());
                if (sec < 0) sec = 0;
                if (sec > 86400) sec = 86400;
            }
            state = trim_str(state);
            for (auto& c : state) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            bool on = (state == "on" || state == "running" || state == "1" || state == "开" ||
                       state == "show" || state == "hold" || state == "auto" || state == "reset");
            ai_bubble_control(is_c2c, group, peer, on, sec);
            continue;
        }
        // 发送节奏：[wait:3] 暂停 3 秒再发后续内容（也接受 "3000ms"，上限 60 秒）
        if (sg.kind == 9) {
            std::string v = trim_str(sg.val);
            for (auto& c : v) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            int ms = (v.size() > 2 && v.substr(v.size() - 2) == "ms")
                     ? std::atoi(v.c_str())
                     : std::atoi(v.c_str()) * 1000;
            if (ms < 0) ms = 0;
            if (ms > 60000) ms = 60000;
            if (ms > 0) {
                std::cout << "[AI] 等待 " << ms << "ms 后继续发送\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
            continue;
        }
        // 工具类指令（exec/read/write/append/ls）由 ai_agent_reply 的**工具循环**处理：
        // 模型请求 → 本机执行 → 结果回填给模型 → 由模型产出自然语言回复。
        // 走到这里说明该指令未被处理（例如已达工具轮次上限），直接丢弃，避免把指令原文发到群里。
        if (sg.kind >= 3) {
            std::cout << "[AGENT] 忽略未处理的工具指令 (kind=" << sg.kind << "): "
                      << sg.val.substr(0, 80) << "\n";
            continue;
        }
        if (sg.kind == 2) {
            // [flood:文本|次数|模式|间隔ms] → 交给刷屏引擎（仅群聊；次数/模式不设上限）
            if (is_c2c) { send_c2c_text(peer, "（刷屏指令仅群聊可用）"); continue; }
            std::string spec = sg.val;
            // 兼容全角分隔符（"｜" 是 UTF-8 多字节，必须按字符串替换，不能用单字符比较）
            size_t wp;
            while ((wp = spec.find("｜")) != std::string::npos) spec.replace(wp, 3, "|");
            std::vector<std::string> fs;
            size_t s0 = 0;
            while (true) {
                size_t bar = spec.find('|', s0);
                if (bar == std::string::npos) { fs.push_back(spec.substr(s0)); break; }
                fs.push_back(spec.substr(s0, bar - s0));
                s0 = bar + 1;
            }
            std::string ftext = fs.empty() ? std::string() : trim_str(fs[0]);
            if (ftext.empty()) ftext = "刷屏";
            int fcount = 10, fdelay = 150;
            std::string fmode = "random";
            if (fs.size() > 1) { int v = std::atoi(trim_str(fs[1]).c_str()); if (v > 0) fcount = v; }
            if (fs.size() > 2) { std::string m = trim_str(fs[2]); if (!m.empty()) fmode = m; }
            if (fs.size() > 3) { int v = std::atoi(trim_str(fs[3]).c_str()); if (v > 0) fdelay = v; }
            std::string fid = flood_start(ftext, fcount, fdelay, 1, fmode, group);
            std::cout << "[AI] 刷屏 " << ftext << " x" << fcount << " mode=" << fmode
                      << " delay=" << fdelay
                      << (fid.empty() ? " 启动失败" : (" 已启动 id=" + fid)) << "\n";
            any_ok = any_ok || !fid.empty();
            continue;
        }
        if (sg.kind == 1) {
            std::string sid = stickers_.find_by_name(sg.val);
            if (sid.empty()) {
                std::string t = "（贴纸未找到：" + sg.val + "）";
                if (is_c2c) send_c2c_text(peer, t); else send_group_text(group, t);
                continue;
            }
            std::string js = sticker_json_of(sid, sg.val);
            bool ok = is_c2c ? send_c2c_sticker(peer, js)
                             : send_group_sticker(group, js, "", "", "");
            std::cout << "[AI] 贴纸 " << sg.val << (ok ? " 已发送" : " 发送失败") << "\n";
            any_ok = any_ok || ok;
            continue;
        }
        std::string body = trim_str(sg.val);
        if (body.empty()) continue;

        std::vector<std::pair<std::string, std::string>> ats;
        if (!is_c2c) {
            // 摘出 @昵称 并解析成真实 @（仅当本地成员缓存能匹配到该昵称）
            std::string out;
            size_t i = 0;
            while (i < body.size()) {
                char c = body[i];
                bool at_start = (c == '@') &&
                                (i == 0 || body[i-1] == ' ' || body[i-1] == '\n' || body[i-1] == '\t');
                if (!at_start) { out += c; i++; continue; }
                size_t e = i + 1;
                while (e < body.size() && body[e] != ' ' && body[e] != '\n' && body[e] != '\t') e++;
                std::string nick = body.substr(i + 1, e - i - 1);
                std::string uid;
                if (!nick.empty() && resolve_member_nick(group, nick, uid)) {
                    ats.push_back(std::make_pair(uid, nick));
                    i = e;
                    if (i < body.size() && body[i] == ' ') i++;
                    continue;
                }
                out += c;
                i++;
            }
            body = trim_str(out);
        }
        if (body.empty() && ats.empty()) continue;

        // 超长文本分条发送（平台单条消息上限约 4500 字符）：
        // 优先在换行处切分，其次在句末标点，最后才硬切
        std::vector<std::string> chunks;
        {
            const size_t LIMIT = 3800;
            size_t p = 0;
            while (p < body.size()) {
                if (body.size() - p <= LIMIT) { chunks.push_back(body.substr(p)); break; }
                size_t cut = p + LIMIT;
                size_t nl = body.rfind('\n', cut);
                if (nl != std::string::npos && nl > p + LIMIT / 2) {
                    cut = nl + 1;
                } else {
                    const char* seps[] = {"。", "！", "？", "…", ".", "!", "?"};
                    size_t best = std::string::npos;
                    for (auto sep : seps) {
                        size_t sl = strlen(sep);
                        size_t q = body.rfind(sep, cut);
                        if (q != std::string::npos && q + sl > p + LIMIT / 2 &&
                            (best == std::string::npos || q + sl > best))
                            best = q + sl;
                    }
                    if (best != std::string::npos) cut = best;
                }
                // 硬切时回退到 UTF-8 字符边界，避免把汉字切成半个（出现乱码）
                while (cut > p && cut < body.size() && ((unsigned char)body[cut] & 0xC0) == 0x80) cut--;
                if (cut <= p) cut = p + LIMIT;
                chunks.push_back(body.substr(p, cut - p));
                p = cut;
            }
            if (chunks.empty()) chunks.push_back(body);
        }
        if (chunks.size() > 1)
            std::cout << "[AI] 长回复分 " << chunks.size() << " 条发送\n";

        // 引用模式（对齐官方 replyToMode）：first=仅首条引用，all=每条都引用，off=不引用
        bool use_ref = !is_c2c && !ref_msg_id.empty() && config_.reply_to_mode != "off";
        if (use_ref) std::cout << "[AI] 引用回复 msg_id=" << ref_msg_id.substr(0, 24) << "\n";

        for (size_t ci = 0; ci < chunks.size(); ci++) {
            std::string chunk = trim_str(chunks[ci]);
            if (chunk.empty()) continue;
            bool ok = false;
            if (is_c2c) {
                ok = send_c2c_text(peer, chunk);
            } else if (use_ref && (ci == 0 || config_.reply_to_mode == "all") && ats.size() <= 1) {
                // 引用触发本回复的那条消息；若解析出单个 @，一并带上
                std::string at_u, at_n;
                if (ci == 0 && !ats.empty()) { at_u = ats[0].first; at_n = ats[0].second; }
                ok = send_group_reply(group, chunk, ref_msg_id, at_u, at_n);
            } else if (ci == 0 && !ats.empty()) {
                ok = send_group_multi_at(group, chunk, ats);
            } else if (ci == 0 && !default_at_user.empty()) {
                ok = send_group_at(group, chunk, default_at_user, default_at_nick);
            } else {
                ok = send_group_text(group, chunk);
            }
            any_ok = any_ok || ok;
        }
    }
    return any_ok;
}

// ═══════════════════════════════════════════════
//  命令执行 / 文件读写（⚠️ 高风险：等于把本机开放为远程 Shell）
// ═══════════════════════════════════════════════

// 启动时的高风险功能警告
std::string YuanbaoServer::exec_startup_warning() const {
    if (!config_.exec_enabled && !config_.file_ops_enabled) return "";
    std::string scope = config_.exec_scope;
    std::string who = (scope == "all") ? "私聊 + 群聊（群里任何人 @机器人 即可用）"
                    : (scope == "private") ? "仅私聊（任何人）"
                    : "仅 OWNER_ID 指定的用户";
    if (!config_.exec_allow_from.empty())
        who += "，且限制在 EXEC_ALLOW_FROM 白名单（" + std::to_string(config_.exec_allow_from.size()) + " 人）内";
    std::string w = "\n[!] 安全提醒：已开启高风险功能 —— ";
    if (config_.exec_enabled) w += "命令执行 ";
    if (config_.file_ops_enabled) w += "文件读写 ";
    if (config_.ai_exec_enabled && config_.exec_enabled) w += "（含 AI 自主执行） ";
    w += "\n    可用范围：" + who;
    w += "\n    这意味着对方可以在本机执行命令 / 读写文件，请确认这是你要的；";
    w += "\n    不需要时把 config.json 的 EXEC_ENABLED / FILE_OPS_ENABLED / AI_EXEC_ENABLED 设为 false。\n";
    return w;
}

// 权限校验：EXEC_ENABLED / EXEC_SCOPE(owner|private|all) / OWNER_ID / EXEC_ALLOW_FROM
bool YuanbaoServer::exec_allowed(bool is_c2c, const std::string& sender_id, std::string& deny) const {
    deny.clear();
    // 总开关：命令执行或文件功能任一开启即可（具体命令各自再校验自己的开关）
    if (!config_.exec_enabled && !config_.file_ops_enabled) {
        deny = "命令执行/文件功能均未开启（EXEC_ENABLED / FILE_OPS_ENABLED）";
        return false;
    }
    if (!config_.exec_allow_from.empty()) {
        bool hit = false;
        for (auto& id : config_.exec_allow_from) if (id == sender_id) { hit = true; break; }
        if (!hit) { deny = "发送者不在 EXEC_ALLOW_FROM 白名单内"; return false; }
    }
    std::string scope = config_.exec_scope;
    if (scope != "all" && scope != "private" && scope != "owner") scope = "owner";
    if (scope == "owner") {
        if (config_.owner_id.empty()) { deny = "未配置 OWNER_ID（EXEC_SCOPE=owner 时必需）"; return false; }
        if (sender_id != config_.owner_id) { deny = "仅 Bot 创建者可用（EXEC_SCOPE=owner）"; return false; }
    } else if (scope == "private") {
        if (!is_c2c) { deny = "仅私聊可用（EXEC_SCOPE=private）"; return false; }
    }
    return true;
}

// 执行 shell 命令，返回合并后的 stdout+stderr 与退出码
std::string YuanbaoServer::run_shell_command(const std::string& cmd, int timeout_sec, bool& timed_out) {
    timed_out = false;
    (void)timeout_sec;
    std::string full = cmd + " 2>&1";
#ifdef YUANBAO_PLATFORM_WINDOWS
    full = "chcp 65001 >nul && " + full;   // 子进程输出统一 UTF-8，避免中文乱码
    if (!config_.exec_cwd.empty())
        full = "cd /d \"" + config_.exec_cwd + "\" && " + full;
#else
    if (!config_.exec_cwd.empty())
        full = "cd \"" + config_.exec_cwd + "\" && " + full;
#endif
#ifdef YUANBAO_PLATFORM_WINDOWS
    // 命令行按系统 ANSI(GBK) 编码交给 cmd.exe，否则中文参数乱码；
    // 子进程内已 chcp 65001，输出为 UTF-8，可直接使用
    std::string full_acp = util::utf8_to_acp(full);
    FILE* fp = popen(full_acp.c_str(), "r");
#else
    FILE* fp = popen(full.c_str(), "r");
#endif
    if (!fp) return "[无法启动进程]";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        out.append(buf, n);
        if (out.size() > 500000) { out += "\n...(输出过多已截断)"; break; }
    }
    int rc = pclose(fp);
    out += "\n[退出码 " + std::to_string(rc) + "]";
    return out;
}

// 独立线程执行并把结果回复到会话（不阻塞消息接收）；结果按纯文本发送，避免被当成富文本指令
void YuanbaoServer::run_exec_async(bool is_c2c, const std::string& group, const std::string& peer,
                                   const std::string& cmd, const std::string& sender_id) {
    std::cout << "[EXEC] " << (is_c2c ? ("私聊 " + peer) : ("群 " + group)) << " 执行: " << cmd << "\n";
    bool c2c = is_c2c;
    std::string g = group, p = peer, c = cmd, sid = sender_id;
    std::thread([this, c2c, g, p, c, sid]() {
        PendingTaskGuard _pg(pending_tasks_);
        bool to = false;
        std::string out = run_shell_command(c, config_.exec_timeout_sec, to);
        if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(输出截断)";
        std::cout << "[EXEC] 输出: " << out.substr(0, 300) << "\n";
        std::string msg = "执行结果：\n" + out;
        bool ok = false;
        if (c2c) {
            ok = send_c2c_text(p, msg);
        } else if (!g.empty() && config_.exec_reply_mode == "private" && !sid.empty()) {
            // 群聊触发但配置为只私聊回执：结果私聊给触发者，群里只留一句提示
            ok = send_c2c_text(sid, msg);
            send_group_text(g, std::string("（命令已执行，结果已私聊发送给你）"));
            std::cout << "[EXEC] 结果已私聊给触发者（EXEC_REPLY_MODE=private）\n";
        } else if (!g.empty()) {
            ok = send_group_text(g, msg);
        }
        std::cout << "[EXEC] 执行完成 (" << (ok ? "已回执" : "回执失败") << ")\n";
    }).detach();
}

// 读文件（相对路径以 EXEC_CWD 为基准）
std::string YuanbaoServer::file_read_text(const std::string& path, bool& ok) {
    ok = false;
    std::string p = path;
    if (!config_.exec_cwd.empty() && p.size() > 1 && p[1] != ':' && p[0] != '/' && p[0] != '\\')
        p = config_.exec_cwd + "/" + p;
    std::string text;
    if (!util::read_file_utf8(p, text)) return "（无法读取：" + p + "）";
    ok = true;
    if (text.size() > 3000) text = text.substr(0, 3000) + "\n...(内容截断)";
    return text;
}

// 文件写入保护：系统目录黑名单 + FILE_ROOTS 白名单 + 拒绝路径穿越
bool YuanbaoServer::file_write_allowed(const std::string& path, std::string& reason) const {
    reason.clear();
    auto norm = [](std::string s) {
        for (auto& c : s) if (c == '\\') c = '/';
#ifdef YUANBAO_PLATFORM_WINDOWS
        for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
#endif
        return s;
    };
    std::string p = norm(path);
    if (p.find("..") != std::string::npos) {
        reason = "路径包含 ..，已拒绝（防目录穿越）";
        return false;
    }
    std::string full = p;
    bool is_abs = (full.size() > 1 && full[1] == ':') || (!full.empty() && full[0] == '/');
    if (!is_abs) {
        std::string base = config_.exec_cwd.empty() ? std::string(".") : config_.exec_cwd;
        full = norm(base) + "/" + full;
    }
    if (config_.file_deny_system) {
        static const char* sys_dirs[] = {
#ifdef YUANBAO_PLATFORM_WINDOWS
            "c:/windows", "c:/program files", "c:/programdata", "c:/$recycle.bin",
            "c:/system volume information",
#else
            "/etc", "/usr", "/bin", "/sbin", "/boot", "/sys", "/proc", "/dev",
            "/system", "/library", "/private",
#endif
        };
        for (auto d : sys_dirs) {
            if (full.rfind(d, 0) == 0) {
                reason = std::string("禁止写入系统目录：") + d;
                return false;
            }
        }
    }
    if (!config_.file_roots.empty()) {
        bool ok = false;
        for (auto& r0 : config_.file_roots) {
            std::string r = norm(r0);
            if (!r.empty() && full.rfind(r, 0) == 0) { ok = true; break; }
        }
        if (!ok) {
            reason = "不在允许的文件根目录（FILE_ROOTS）内";
            return false;
        }
    }
    return true;
}

// ═══════════════════════════════════════════════
//  MCP 客户端（stdio JSON-RPC；每次调用启动一次 server，简单可靠）
// ═══════════════════════════════════════════════

// 前置声明（定义在工具循环处）
static std::vector<std::pair<std::string, std::string>> parse_tool_calls(const std::string& text);

static std::string trim_s(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
}

// MCP 客户端握手前缀（initialize + initialized）
static std::string mcp_handshake(const std::string& ver) {
    return "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
           "\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},"
           "\"clientInfo\":{\"name\":\"yuanbao-bot\",\"version\":\"" + ver + "\"}}}\n"
           "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n";
}

// 启动 server：把 JSON-RPC 请求灌入 stdin，收集 stdout
bool YuanbaoServer::mcp_run(const McpServer& srv, const std::string& requests, std::string& out) {
    out.clear();
#ifdef YUANBAO_PLATFORM_WINDOWS
    // cmd 的 type/重定向不认正斜杠路径，这里统一用反斜杠
    std::string req_path = "logs\\mcp_req_" + srv.name + ".jsonl";
    std::string resp_path = "logs\\mcp_resp_" + srv.name + ".txt";
#else
    std::string req_path = "logs/mcp_req_" + srv.name + ".jsonl";
    std::string resp_path = "logs/mcp_resp_" + srv.name + ".txt";
#endif
    {
        std::ofstream f(req_path, std::ios::binary);
        if (!f.is_open()) return false;
        f.write(requests.data(), (std::streamsize)requests.size());
    }
#ifdef YUANBAO_PLATFORM_WINDOWS
    std::string cmd = "type \"" + req_path + "\" | \"" + srv.command + "\"";
#else
    std::string cmd = "cat \"" + req_path + "\" | \"" + srv.command + "\"";
#endif
    for (auto& a : srv.args) cmd += " \"" + a + "\"";
#ifdef YUANBAO_PLATFORM_WINDOWS
    cmd += " > \"" + resp_path + "\" 2>nul";
#else
    cmd += " > \"" + resp_path + "\" 2>/dev/null";
#endif
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) pclose(fp);
    util::read_file_utf8(resp_path, out);
    std::remove(req_path.c_str());
    std::remove(resp_path.c_str());
    return !out.empty();
}

// 启动时对每个 server 做 initialize + tools/list，缓存工具清单
void YuanbaoServer::mcp_init_all() {
    if (!config_.mcp_enabled || config_.mcp_servers.empty()) return;
    for (auto& srv : config_.mcp_servers) {
        srv.tools.clear();
        srv.ready = false;
        std::string req = mcp_handshake(kYuanbaoBotVersion);
        req += "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n";
        std::string out;
        if (!mcp_run(srv, req, out)) {
            std::cout << "[MCP] " << srv.name << " 启动失败（命令: " << srv.command << "）\n";
            continue;
        }
        std::istringstream is(out);
        std::string line;
        while (std::getline(is, line)) {
            if (line.find("\"id\":2") == std::string::npos) continue;
            JsonVal v;
            if (!json_parse(line, v)) continue;
            for (auto& t : v["result"]["tools"].arr) {
                McpTool mt;
                mt.name = t["name"].asString();
                mt.description = t["description"].asString();
                mt.input_schema = json_compact(t["inputSchema"]);
                if (!mt.name.empty()) srv.tools.push_back(mt);
            }
        }
        srv.ready = true;
        std::cout << "[MCP] " << srv.name << " 就绪，工具 " << srv.tools.size() << " 个\n";
        for (auto& t : srv.tools)
            std::cout << "        - " << srv.name << "." << t.name << "  " << t.description << "\n";
    }
}

// 调用 MCP 工具：server_tool = "服务器.工具"
std::string YuanbaoServer::mcp_call(const std::string& server_tool, const std::string& args_json) {
    size_t dot = server_tool.find('.');
    if (dot == std::string::npos) return "（格式应为 服务器.工具）";
    std::string sname = trim_s(server_tool.substr(0, dot));
    std::string tname = trim_s(server_tool.substr(dot + 1));
    for (auto& srv : config_.mcp_servers) {
        if (srv.name != sname) continue;
        std::string req = mcp_handshake(kYuanbaoBotVersion);
        req += "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" +
               tname + "\",\"arguments\":" + (args_json.empty() ? "{}" : args_json) + "}}\n";
        std::string out;
        if (!mcp_run(srv, req, out)) return "（MCP server 无响应）";
        std::istringstream is(out);
        std::string line;
        while (std::getline(is, line)) {
            if (line.find("\"id\":2") == std::string::npos) continue;
            JsonVal v;
            if (!json_parse(line, v)) continue;
            std::string em = v["error"]["message"].asString();
            if (!em.empty()) return "（MCP 错误：" + em + "）";
            std::string text;
            for (auto& c : v["result"]["content"].arr) {
                std::string tp = c["type"].asString();
                if (tp == "text") text += c["text"].asString() + "\n";
                else text += "[" + (tp.empty() ? "content" : tp) + "] " + json_compact(c) + "\n";
            }
            if (text.empty()) text = json_compact(v["result"]);
            if (text.size() > 6000) text = text.substr(0, 6000) + "\n...(截断)";
            return text;
        }
        return "（未解析到 MCP 响应）";
    }
    return "（未知 MCP 服务器：" + sname + "）";
}

// 供 system prompt 注入的工具清单
std::string YuanbaoServer::mcp_tools_prompt() const {
    if (!config_.mcp_enabled) return "";
    std::string p;
    for (auto& s : config_.mcp_servers) {
        for (auto& t : s.tools) {
            p += "  [mcp:" + s.name + "." + t.name + "|{JSON参数}]  " + t.description + "\n";
            if (!t.input_schema.empty())
                p += "     参数: " + t.input_schema.substr(0, 300) + "\n";
        }
    }
    if (p.empty()) return "";
    return "\n【MCP 工具】（调用格式 [mcp:服务器.工具|JSON参数]，参数必须是合法 JSON）\n" + p;
}

// ═══════════════════════════════════════════════
//  联网搜索 / 网页抓取
// ═══════════════════════════════════════════════

static std::string url_encode_q(const std::string& s) {
    std::string out;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// HTML → 纯文本（去标签、去脚本、解实体、压缩空白、截断）
static std::string html_to_text(const std::string& s, size_t max_len) {
    std::string out;
    bool in_tag = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '<') {
            // 跳过 script/style 整段
            auto starts = [&](const char* tag) {
                return s.compare(i, strlen(tag), tag) == 0;
            };
            if (starts("<script") || starts("<style")) {
                const char* close = starts("<script") ? "</script>" : "</style>";
                size_t e = s.find(close, i);
                i = (e == std::string::npos) ? s.size() : e + strlen(close) - 1;
                continue;
            }
            in_tag = true;
            continue;
        }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out += c;
    }
    auto repl = [&](const char* from, const char* to) {
        size_t p = 0;
        while ((p = out.find(from, p)) != std::string::npos) {
            out.replace(p, strlen(from), to);
            p += strlen(to);
        }
    };
    repl("&amp;", "&");
    repl("&lt;", "<");
    repl("&gt;", ">");
    repl("&quot;", "\"");
    repl("&#39;", "'");
    repl("&nbsp;", " ");
    repl("&ensp;", " ");
    repl("&emsp;", " ");
    repl("&hellip;", "…");
    repl("&mdash;", "—");
    repl("&ndash;", "–");
    repl("&ldquo;", "“");
    repl("&rdquo;", "”");
    repl("&lsquo;", "‘");
    repl("&rsquo;", "’");
    // 其余数字实体（&#123; / &#x1F;）统一替换为空格，避免干扰阅读
    out = std::regex_replace(out, std::regex("&#[0-9]+;"), " ");
    out = std::regex_replace(out, std::regex("&#[xX][0-9a-fA-F]+;"), " ");
    std::string t;
    bool sp = false;
    for (char c : out) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { sp = true; continue; }
        if (sp && !t.empty()) t += ' ';
        sp = false;
        t += c;
    }
    if (t.size() > max_len) t = t.substr(0, max_len) + "…";
    return t;
}

// https://host[:port]/path → host/port/path
static bool split_url(const std::string& url, std::string& host, int& port, std::string& path) {
    std::string u = url;
    if (u.rfind("https://", 0) == 0) { u = u.substr(8); port = 443; }
    else if (u.rfind("http://", 0) == 0) { u = u.substr(7); port = 80; }
    else return false;
    size_t slash = u.find('/');
    std::string hp = (slash == std::string::npos) ? u : u.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : u.substr(slash);
    size_t colon = hp.find(':');
    if (colon != std::string::npos) {
        host = hp.substr(0, colon);
        port = std::atoi(hp.substr(colon + 1).c_str());
    } else {
        host = hp;
    }
    return !host.empty();
}

// 用 curl 抓取页面（自动跟随重定向、解压 gzip、处理 chunked）
// 说明：内置 https_request 对分块大响应可能提前结束，抓网页用 curl 更可靠
static bool curl_fetch(const std::string& url, std::string& out) {
    out.clear();
    if (!is_safe_url(url)) return false;
    std::string tmp = "logs/fetch_tmp.html";
    std::string cmd =
        "curl -sL --compressed -k --connect-timeout 10 -m 25 "
        "-A \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" "
        "-H \"Accept-Language: zh-CN,zh;q=0.9\" \"" + url + "\" -o \"" + tmp + "\"";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) pclose(fp);
    util::read_file_utf8(tmp, out);
    std::remove(tmp.c_str());
    return !out.empty();
}

// 拒绝私有/环回地址（防 SSRF）
static bool is_private_host(const std::string& host) {
    std::string h = host;
    for (auto& c : h) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (h == "localhost" || h == "0.0.0.0" || h == "[::1]" || h == "::1") return true;
    if (h.rfind("127.", 0) == 0 || h.rfind("10.", 0) == 0 ||
        h.rfind("192.168.", 0) == 0 || h.rfind("169.254.", 0) == 0) return true;
    if (h.rfind("172.", 0) == 0) {   // 172.16.0.0 - 172.31.255.255
        int second = std::atoi(h.c_str() + 4);
        if (second >= 16 && second <= 31) return true;
    }
    return false;
}

std::string YuanbaoServer::web_search(const std::string& query, int count) {
    if (!config_.web_search_enabled)
        return "（联网搜索未开启：config.json 设 WEB_SEARCH_ENABLED=true）";
    if (query.empty()) return "（搜索关键词为空）";
    if (count <= 0) count = config_.search_count;
    if (count > 10) count = 10;

    std::map<std::string, std::string> hdr;
    hdr["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    hdr["Accept-Language"] = "zh-CN,zh;q=0.9,en;q=0.8";

    // 1) 配置了搜索 API（Tavily 兼容：POST {query,max_results} + Bearer）
    if (!config_.search_api_url.empty()) {
        std::string host, path;
        int port = 443;
        if (!split_url(config_.search_api_url, host, port, path))
            return "（SEARCH_API_URL 无法解析）";
        if (is_private_host(host)) return "（拒绝访问私有地址）";
        if (!config_.search_api_key.empty())
            hdr["Authorization"] = "Bearer " + config_.search_api_key;
        std::string req = "{\"query\":\"" + proto::json_escape_str(query) +
                          "\",\"max_results\":" + std::to_string(count) + "}";
        std::string out;
        https_request("POST", host, port, path, req, "application/json", hdr, out);
        JsonVal v;
        if (!json_parse(out, v) || v["results"].arr.empty())
            return "（搜索 API 无结果或返回异常：" + out.substr(0, 200) + "）";
        std::string res;
        int i = 0;
        for (auto& r : v["results"].arr) {
            if (++i > count) break;
            res += std::to_string(i) + ". " + r["title"].asString() + "\n   " +
                   r["url"].asString() + "\n   " +
                   html_to_text(r["content"].asString(), 240) + "\n";
        }
        std::cout << "[SEARCH] \"" << query << "\" → " << (i - 1) << " 条（API）\n";
        return res;
    }

    // 2) 未配置 API：抓 Bing HTML 兜底（用 curl：自动跟随重定向、解压 gzip）
    std::string url = "https://cn.bing.com/search?q=" + url_encode_q(query) +
                      "&count=" + std::to_string(count) + "&setlang=zh-CN";
    std::string out;
    if (!curl_fetch(url, out))
        return "（搜索请求失败，请检查网络，或在 config.json 配置 SEARCH_API_URL）";
    // 注意：普通字符串需转义引号与反斜杠（raw string 会被内容里的 )" 提前终止）
    std::regex re("<h2[^>]*>\\s*<a[^>]*href=\"(https?://[^\"]+)\"[^>]*>([\\s\\S]*?)</a>",
                  std::regex::icase);
    std::string res;
    int i = 0;
    for (std::sregex_iterator it(out.begin(), out.end(), re), end; it != end; ++it) {
        if (++i > count) break;
        std::string url = (*it)[1].str();
        std::string title = html_to_text((*it)[2].str(), 120);
        std::string snippet;
        size_t pos = (size_t)it->position(0) + (size_t)it->length(0);
        size_t p1 = out.find("<p", pos);
        if (p1 != std::string::npos && p1 - pos < 2000) {
            size_t p2 = out.find('>', p1);
            size_t p3 = (p2 == std::string::npos) ? std::string::npos : out.find("</p>", p2);
            if (p2 != std::string::npos && p3 != std::string::npos)
                snippet = html_to_text(out.substr(p2 + 1, p3 - p2 - 1), 240);
        }
        res += std::to_string(i) + ". " + (title.empty() ? "(无标题)" : title) + "\n   " + url + "\n";
        if (!snippet.empty()) res += "   " + snippet + "\n";
    }
    std::cout << "[SEARCH] \"" << query << "\" → " << (i > count ? count : i) << " 条（Bing）\n";
    if (res.empty()) {
        // 调试：把原始响应存下来，便于排查（gzip/反爬/结构变化）
        {
            std::ofstream dbg("logs/search_debug.html", std::ios::binary);
            if (dbg.is_open()) dbg.write(out.data(), (std::streamsize)out.size());
        }
        std::cout << "[SEARCH] 解析失败：HTML " << out.size() << " 字节，已存 logs/search_debug.html\n";
        return "（未解析到搜索结果；可在 config.json 配置 SEARCH_API_URL 使用搜索 API）";
    }
    return res;
}

std::string YuanbaoServer::web_fetch_page(const std::string& url) {
    if (!config_.web_fetch_enabled)
        return "（网页抓取未开启：config.json 设 WEB_FETCH_ENABLED=true）";
    if (!is_safe_url(url)) return "（URL 不合法）";
    std::string host, path;
    int port = 443;
    if (!split_url(url, host, port, path)) return "（URL 无法解析）";
    if (is_private_host(host)) return "（拒绝访问私有地址）";
    std::string body;
    if (!curl_fetch(url, body)) return "（抓取失败：无响应）";
    std::string text = html_to_text(body, 4000);
    std::cout << "[FETCH] " << url << " → " << text.size() << " 字符\n";
    return text.empty() ? "（页面无可读正文）" : text;
}

// ═══════════════════════════════════════════════
//  Skill（OpenClaw / AgentSkills 兼容：skills/<name>/SKILL.md）
// ═══════════════════════════════════════════════

void YuanbaoServer::scan_skills() {
    skills_.clear();
    if (!config_.skills_enabled) return;
    std::vector<std::string> dirs = config_.skills_dirs;
    dirs.push_back("skills");   // 默认工作区目录
    for (auto& dir : dirs) {
        std::vector<std::string> subs;
#ifdef YUANBAO_PLATFORM_WINDOWS
        std::wstring pat = util::utf8_to_wide(dir) + L"\\*";
        WIN32_FIND_DATAW ffd;
        HANDLE h = FindFirstFileW(pat.c_str(), &ffd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::string n = util::wide_to_utf8(std::wstring(ffd.cFileName));
            if (n == "." || n == "..") continue;
            subs.push_back(n);
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
#else
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        struct dirent* ent;
        while ((ent = readdir(d))) {
            std::string n = ent->d_name;
            if (n == "." || n == "..") continue;
            subs.push_back(n);
        }
        closedir(d);
#endif
        for (auto& sub : subs) {
            std::string path = dir + "/" + sub + "/SKILL.md";
            std::string content;
            if (!util::read_file_utf8(path, content) || content.empty()) continue;
            SkillInfo si;
            si.path = path;
            si.name = sub;
            std::string body = content;
            size_t p1 = content.find("---");
            if (p1 != std::string::npos && p1 < 8) {
                size_t p2 = content.find("\n---", p1 + 3);
                if (p2 != std::string::npos) {
                    std::string fm = content.substr(p1 + 3, p2 - (p1 + 3));
                    body = content.substr(p2 + 4);
                    std::istringstream is(fm);
                    std::string line;
                    while (std::getline(is, line)) {
                        size_t c = line.find(':');
                        if (c == std::string::npos) continue;
                        std::string k = trim_s(line.substr(0, c));
                        std::string v = trim_s(line.substr(c + 1));
                        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') ||
                                              (v.front() == '\'' && v.back() == '\'')))
                            v = v.substr(1, v.size() - 2);
                        if (k == "name" && !v.empty()) si.name = v;
                        else if (k == "description") si.description = v;
                    }
                }
            }
            si.body = trim_s(body);
            if (!si.body.empty()) {
                skills_.push_back(si);
                std::cout << "[SKILL] 加载 " << si.name << "  (" << si.path << ")\n";
            }
        }
    }
    if (!skills_.empty()) std::cout << "[SKILL] 共 " << skills_.size() << " 个技能\n";
}

// 只列名称与描述（渐进式披露：正文按需取）
std::string YuanbaoServer::skills_prompt() const {
    if (skills_.empty()) return "";
    std::string p = "\n【可用技能】（需要时输出 [skill:名称] 获取完整指令）\n";
    for (auto& s : skills_) p += "  " + s.name + " - " + s.description + "\n";
    return p;
}

std::string YuanbaoServer::skill_body(const std::string& name) const {
    for (auto& s : skills_) {
        if (s.name == name) {
            std::string b = s.body;
            if (b.size() > 8000) b = b.substr(0, 8000) + "\n...(截断)";
            return b;
        }
    }
    return "";
}

// ═══════════════════════════════════════════════
//  子代理（独立会话 + 独立工具循环，禁止再派生）
// ═══════════════════════════════════════════════
std::string YuanbaoServer::ai_subagent(const std::string& task, bool is_c2c, const std::string& group,
                                       const std::string& sender_id) {
    if (task.empty()) return "（任务为空）";
    int active = subagent_active_.load();
    if (active >= config_.subagent_max_concurrent)
        return "（子代理并发已达上限 " + std::to_string(config_.subagent_max_concurrent) +
               "，请稍后再试）";
    subagent_active_++;
    std::cout << "[SUBAGENT] 启动（并发 " << (active + 1) << "）: " << task.substr(0, 100) << "\n";

    std::vector<std::pair<std::string, std::string>> msgs;
    std::string sys = "你是一个子代理，负责完成主代理委派的子任务。";
    if (config_.exec_enabled) sys += "可用 [exec:命令] 执行命令；";
    if (config_.file_ops_enabled) sys += "可用 [read:路径]/[ls:目录]/[write:路径|内容] 读写文件；";
    if (config_.mcp_enabled) sys += "可用 [mcp:服务器.工具|JSON参数] 调用 MCP 工具；";
    sys += "完成后用简洁中文汇报结果（不要输出无关寒暄）。你不能再派生其他子代理。";
    msgs.push_back({"system", sys});
    msgs.push_back({"user", task});

    std::string reply;
    const int MAX_ROUNDS = 4;
    for (int round = 0; round < MAX_ROUNDS; round++) {
        reply = llm_chat(msgs);
        std::cout << "[SUBAGENT] 第 " << (round + 1) << " 轮回复 " << reply.size()
                  << " 字节: " << reply.substr(0, 100) << "\n";
        if (reply.empty() && round == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            reply = llm_chat(msgs);
            std::cout << "[SUBAGENT] 重试回复 " << reply.size() << " 字节\n";
        }
        if (reply.empty()) break;
        auto calls = parse_tool_calls(reply);
        if (calls.empty()) break;
        if ((int)calls.size() > 6) calls.resize(6);
        msgs.push_back({"assistant", reply});
        std::string results;
        for (auto& c : calls) {
            std::string out = exec_tool_for_model(c.first, c.second, is_c2c, group, sender_id,
                                                  /*allow_agent=*/false);
            std::cout << "[SUBAGENT] " << c.first << " " << c.second.substr(0, 80)
                      << " → " << out.substr(0, 120) << "\n";
            results += "【" + c.first + ": " + c.second + "】\n" + out + "\n\n";
        }
        msgs.push_back({"user", "（工具结果如下，请据此用自然语言汇报，不要复述原始输出）\n" + results});
    }
    subagent_active_--;
    std::cout << "[SUBAGENT] 完成，结果 " << reply.size() << " 字节\n";
    return reply.empty() ? "（子代理未产出结果）" : reply;
}

// 写/追加文件
bool YuanbaoServer::file_write_text(const std::string& path, const std::string& content,
                                    bool append, std::string& err) {
    err.clear();
    std::string p = path;
    if (!config_.exec_cwd.empty() && p.size() > 1 && p[1] != ':' && p[0] != '/' && p[0] != '\\')
        p = config_.exec_cwd + "/" + p;
    std::string reason;
    if (!file_write_allowed(p, reason)) { err = reason; return false; }
    std::ios::openmode mode = append ? (std::ios::app | std::ios::binary)
                                     : (std::ios::trunc | std::ios::binary);
    std::ofstream f(p.c_str(), mode);
    if (!f.is_open()) { err = "无法打开文件：" + p; return false; }
    f.write(content.data(), (std::streamsize)content.size());
    if (!f.good()) { err = "写入失败：" + p; return false; }
    f.close();
    return true;
}

// 列目录
std::string YuanbaoServer::dir_list_text(const std::string& path, bool& ok) {
    ok = false;
    std::string p = path;
    if (p.empty()) p = config_.exec_cwd.empty() ? std::string(".") : config_.exec_cwd;
    else if (!config_.exec_cwd.empty() && p.size() > 1 && p[1] != ':' && p[0] != '/' && p[0] != '\\')
        p = config_.exec_cwd + "/" + p;
    std::string out;
#ifdef YUANBAO_PLATFORM_WINDOWS
    std::wstring pat = util::utf8_to_wide(p) + L"\\*";
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(pat.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return "（无法列出目录：" + p + "）";
    int n = 0;
    do {
        std::string name = util::wide_to_utf8(std::wstring(ffd.cFileName));
        if (name == "." || name == "..") continue;
        bool is_dir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out += is_dir ? "[目录] " : "        ";
        out += name;
        out += "\n";
        if (++n >= 200) { out += "...(更多省略)\n"; break; }
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
#else
    DIR* d = opendir(p.c_str());
    if (!d) return "（无法列出目录：" + p + "）";
    struct dirent* ent;
    int n = 0;
    while ((ent = readdir(d))) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string full = p + "/" + name;
        struct stat st;
        bool is_dir = (stat(full.c_str(), &st) == 0) && S_ISDIR(st.st_mode);
        out += is_dir ? "[目录] " : "        ";
        out += name;
        out += "\n";
        if (++n >= 200) { out += "...(更多省略)\n"; break; }
    }
    closedir(d);
#endif
    ok = true;
    return out.empty() ? "（目录为空）" : out;
}

// ═══════════════════════════════════════════════
//  定时任务（/remind 一次性、/cron 周期）
// ═══════════════════════════════════════════════

static std::string fmt_ms_time(int64_t ms) {
    time_t tt = (time_t)(ms / 1000);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[40];
    strftime(buf, sizeof(buf), "%m-%d %H:%M", &tmv);
    return std::string(buf);
}

bool YuanbaoServer::parse_when(const std::string& s, int64_t& next_at, int64_t& interval_ms) {
    next_at = 0;
    interval_ms = 0;
    std::string t = s;
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
    if (t.empty()) return false;

    bool all_digit = true;
    for (char c : t) if (c < '0' || c > '9') { all_digit = false; break; }
    if (all_digit) {   // 纯数字 = 分钟
        long long v = std::atoll(t.c_str());
        if (v <= 0) return false;
        next_at = (int64_t)util::now_ms() + v * 60000;
        return true;
    }

    char unit = t.back();
    if (unit == 's' || unit == 'S' || unit == 'm' || unit == 'M' ||
        unit == 'h' || unit == 'H' || unit == 'd' || unit == 'D') {
        long long v = std::atoll(t.substr(0, t.size() - 1).c_str());
        if (v <= 0) return false;
        int64_t ms = v * (unit == 's' || unit == 'S' ? 1000
                        : unit == 'm' || unit == 'M' ? 60000
                        : unit == 'h' || unit == 'H' ? 3600000
                                                     : 86400000);
        next_at = (int64_t)util::now_ms() + ms;
        return true;
    }

    size_t colon = t.find(':');
    if (colon != std::string::npos) {   // HH:MM（今天已过则明天）
        int hh = std::atoi(t.substr(0, colon).c_str());
        int mm = std::atoi(t.substr(colon + 1).c_str());
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
        time_t now = time(nullptr);
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        tmv.tm_hour = hh;
        tmv.tm_min = mm;
        tmv.tm_sec = 0;
        time_t target = mktime(&tmv);
        if (target <= now) target += 86400;
        next_at = (int64_t)target * 1000;
        return true;
    }
    return false;
}

void YuanbaoServer::task_load() {
    std::string content;
    if (!util::read_file_utf8("logs/tasks.json", content)) return;
    JsonVal v;
    if (!json_parse(content, v)) return;
    std::lock_guard<std::mutex> l(task_mu_);
    for (auto& it : v["tasks"].arr) {
        ScheduledTask t;
        t.id = it["id"].asString();
        t.kind = it["kind"].asString();
        t.is_c2c = it["is_c2c"].asBool();
        t.group = it["group"].asString();
        t.peer = it["peer"].asString();
        t.content = it["content"].asString();
        t.next_at = (int64_t)it["next_at"].asDouble();
        t.interval_ms = (int64_t)it["interval_ms"].asDouble();
        if (!t.id.empty() && !t.content.empty()) tasks_[t.id] = t;
    }
    if (!tasks_.empty()) std::cout << "[TASK] 已加载 " << tasks_.size() << " 个定时任务\n";
}

void YuanbaoServer::task_save_locked() {
#ifdef _WIN32
    _mkdir("logs");
#else
    mkdir("logs", 0755);
#endif
    JsonVal out;
    for (auto& kv : tasks_) {
        JsonVal e;
        e["id"] = kv.second.id;
        e["kind"] = kv.second.kind;
        e["is_c2c"] = kv.second.is_c2c;
        e["group"] = kv.second.group;
        e["peer"] = kv.second.peer;
        e["content"] = kv.second.content;
        e["next_at"] = (double)kv.second.next_at;
        e["interval_ms"] = (double)kv.second.interval_ms;
        out["tasks"].arr.push_back(e);
    }
    std::ofstream f("logs/tasks.json");
    if (f.is_open()) { f << json_compact(out); f.close(); }
}

void YuanbaoServer::task_loop() {
    while (task_thread_started_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int64_t now = (int64_t)util::now_ms();
        std::vector<ScheduledTask> due;
        {
            std::lock_guard<std::mutex> l(task_mu_);
            for (auto it = tasks_.begin(); it != tasks_.end(); ) {
                if (it->second.next_at > 0 && it->second.next_at <= now) {
                    due.push_back(it->second);
                    if (it->second.interval_ms > 0) {
                        it->second.next_at = now + it->second.interval_ms;
                        ++it;
                    } else {
                        it = tasks_.erase(it);   // 一次性任务触发后删除
                    }
                } else {
                    ++it;
                }
            }
            if (!due.empty()) task_save_locked();
        }
        for (auto& t : due) {
            bool ok = t.is_c2c ? send_c2c_text(t.peer, t.content)
                               : send_group_text(t.group, t.content);
            std::cout << "[TASK] 触发 " << t.id << " (" << t.kind << ") → "
                      << (t.is_c2c ? ("私聊 " + t.peer) : ("群 " + t.group))
                      << " " << (ok ? "OK" : "FAIL") << "\n";
        }
    }
}

void YuanbaoServer::task_start() {
    if (task_thread_started_.exchange(true)) return;
    task_thread_ = std::thread([this]() { task_loop(); });
}

// 与 Python sender.py `_get_upload_info` + `_upload_to_cos` 一致
bool YuanbaoServer::upload_media(const std::string& file_name, const std::string& file_data,
                                 std::string& out_url, std::string& out_uuid) {
    if (file_name.empty() || file_data.empty()) return false;
    if (!bot_sign_token()) return false;  // 确保有 token

    std::string file_id = util::random_hex_id();  // uuid4().hex
    out_uuid = file_id;

    std::string host = config_.api_domain;
    int port = 443;
    if (host.find("https://") == 0) host = host.substr(8);
    else if (host.find("http://") == 0) host = host.substr(7);
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = safe_stoi(host.substr(colon + 1), port);
        host = host.substr(0, colon);
    }

    std::string resp_body;
    std::map<std::string, std::string> extra;
    extra["X-ID"] = config_.yuanbao_id;
    extra["X-Token"] = client_token_;
    extra["X-Source"] = "web";
    extra["X-AppVersion"] = "2.0.1";
    extra["X-OperationSystem"] = "Linux";
    extra["X-Instance-Id"] = "99";

    std::ostringstream body;
    body << "{\"fileName\":" << json_quote(file_name)
         << ",\"fileId\":\"" << file_id << "\""
         << ",\"docFrom\":\"localDoc\",\"docOpenId\":\"\"}";

    std::string api_path = "/api/resource/genUploadInfo";
    int ret;
#ifdef YUANBAO_NO_OPENSSL
    {
        // 无 OpenSSL 时使用 curl 命令行（与 sign-token 一致）
        std::ostringstream cmd;
        cmd << "curl -s -k -X POST \"https://" << host << ":" << port << api_path << "\""
            << " -H \"Content-Type: application/json\""
            << " -H \"X-ID: " << config_.yuanbao_id << "\""
            << " -H \"X-Token: " << client_token_ << "\""
            << " -H \"X-Source: web\""
            << " -H \"X-AppVersion: 2.0.1\""
            << " -H \"X-OperationSystem: Linux\""
            << " -H \"X-Instance-Id: 99\""
            << " -d '" << body.str() << "'";
        FILE* fp = popen(cmd.str().c_str(), "r");
        if (!fp) return false;
        char buf2[65536];
        size_t n2 = fread(buf2, 1, sizeof(buf2) - 1, fp);
        buf2[n2] = '\0';
        pclose(fp);
        resp_body = std::string(buf2, n2);
        ret = 0;
    }
#else
    ret = https_request("POST", host, port, api_path, body.str(),
                        "application/json", extra, resp_body, 30000);
#endif

    if (ret != 0) {
        std::cerr << "[Bot] genUploadInfo 请求失败 ret=" << ret << "\n";
        return false;
    }

    // genUploadInfo 响应为平铺结构（无 code/data 包装，与 Python result.get("data", result) 一致）：
    // {"error":{},"isUploaded":false,"bucketName":...,"region":...,"location":...,
    //  "encryptTmpSecretId":...,"encryptTmpSecretKey":...,"encryptToken":...,
    //  "startTime":<num>,"expiredTime":<num>,"resourceUrl":...}
    JsonVal root;
    if (!json_parse(resp_body, root)) {
        std::cerr << "[Bot] genUploadInfo 响应解析失败: " << resp_body.substr(0, 300) << "\n";
        return false;
    }

    // 兼容两种结构：平铺响应（新接口）或 {code,data:{...}} 包装（旧接口）
    JsonVal data = root["data"];
    bool is_flat = data.empty() && !root.empty();
    const JsonVal& d = is_flat ? root : data;

    std::string secret_id = d["encryptTmpSecretId"].asString();
    std::string secret_key = d["encryptTmpSecretKey"].asString();
    std::string security_token = d["encryptToken"].asString();
    std::string start_time = d["startTime"].asString();
    std::string expired_time = d["expiredTime"].asString();
    std::string bucket = d["bucketName"].asString();
    std::string region = d["region"].asString();
    std::string location = d["location"].asString();
    std::string resource_url = d["resourceUrl"].asString();

    // startTime/expiredTime 是数字类型，asString() 取不到，需转字符串
    if (start_time.empty() || expired_time.empty()) {
        double st = d["startTime"].asDouble();
        double et = d["expiredTime"].asDouble();
        if (st > 0) start_time = std::to_string((long long)st);
        if (et > 0) expired_time = std::to_string((long long)et);
    }

    if (bucket.empty() || region.empty() || location.empty() || secret_key.empty()) {
        std::cerr << "[Bot] COS 凭证不完整: " << resp_body.substr(0, 300) << "\n";
        return false;
    }


    // COS 签名（与 Python _upload_to_cos 手动签名一致）
    std::string key_time = start_time + ";" + expired_time;
    std::string sign_key = hmac_sha1_hex(secret_key, key_time);
    std::string cos_host = bucket + ".cos." + region + ".myqcloud.com";
    std::string http_string = "put\n" + location + "\n\nhost=" + cos_host + "\n";
    std::string string_to_sign = "sha1\n" + key_time + "\n" + sha1_hex(http_string) + "\n";
    std::string signature = hmac_sha1_hex(sign_key, string_to_sign);

    std::string authorization =
        "q-sign-algorithm=sha1&q-ak=" + secret_id +
        "&q-sign-time=" + key_time +
        "&q-key-time=" + key_time +
        "&q-header-list=host&q-url-param-list=&q-signature=" + signature;
    if (!security_token.empty())
        authorization += "&x-cos-security-token=" + security_token;

    std::string upload_url = "https://" + cos_host + location;
    std::map<std::string, std::string> cos_headers;
    cos_headers["Authorization"] = authorization;
    if (!security_token.empty())
        cos_headers["x-cos-security-token"] = security_token;

    std::string cos_resp;
#ifdef YUANBAO_NO_OPENSSL
    {
        std::ostringstream cmd;
        cmd << "curl -s -k -X PUT \"" << upload_url << "\""
            << " -H \"Host: " << cos_host << "\""
            << " -H \"Authorization: " << authorization << "\""
            << " -H \"Content-Type: application/octet-stream\"";
        if (!security_token.empty())
            cmd << " -H \"x-cos-security-token: " << security_token << "\"";
        // 通过 stdin 传入二进制数据
        cmd << " --data-binary @-";
        FILE* fp = popen(cmd.str().c_str(), "w");
        if (!fp) return false;
        fwrite(file_data.data(), 1, file_data.size(), fp);
        pclose(fp);
        cos_resp = "";
        ret = 0;
    }
#else
    ret = https_request("PUT", cos_host, 443, location, file_data,
                        "application/octet-stream", cos_headers, cos_resp, 60000);
#endif
    if (ret != 0) {
        std::cerr << "[Bot] COS 上传失败 ret=" << ret << " resp=" << cos_resp.substr(0, 200) << "\n";
        return false;
    }


    // ← 修复：保存上传凭证，供 /api/image-proxy 对 _64 域图片重新签名 GET URL 下载
    {
        std::lock_guard<std::mutex> l(members_mu_);
        std::cerr << "[Bot] upload_media save sk=" << secret_key.substr(0, 4) << " keytime=" << key_time << "\n";
        cos_upload_region_ = region;
        cos_upload_bucket_ = bucket;
        cos_upload_location_ = location;
        cos_upload_secret_id_ = secret_id;
        cos_upload_secret_key_ = secret_key;
        cos_upload_token_ = security_token;
        cos_upload_key_time_ = key_time;
        cos_upload_expired_ = (int64_t)strtoll(expired_time.c_str(), nullptr, 10);
    }

    // ← 修复：对齐撤回提示的图片补发逻辑（handle_recall_notification）——
    //   直接用 genUploadInfo 返回的 resourceUrl（不 resolve、不签名）。
    //   撤回补发正是用 media_info.image_urls（resourceUrl）直接 send_group_image 成功，
    //   说明服务端能通过 resourceUrl 引用图片。
    out_url = resource_url;
    return true;
}

bool YuanbaoServer::send_group_image(const std::string& group, const std::string& url,
                                     const std::string& uuid, int size, int w, int h,
                                     const std::string& at_user, const std::string& at_nick) {
    if (group.empty() || url.empty()) return false;
    // 与 Python send_group_image_message / _build_at_message 一致
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body;
    bytes_append(body, proto::encode_string(1, body_msg_id));
    bytes_append(body, proto::encode_string(2, group));
    bytes_append(body, proto::encode_string(3, config_.yuanbao_id));
    bytes_append(body, proto::encode_string(4, ""));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000);
    bytes_append(body, proto::encode_string(5, std::to_string(rnd)));  // string 类型
    if (!at_user.empty())
        bytes_append(body, proto::encode_message(6, proto::encode_at_custom_elem(at_user, at_nick)));
    bytes_append(body, proto::encode_message(6, proto::encode_image_elem(url, uuid, size, w, h)));
    bytes_append(body, proto::encode_string(7, ""));
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    auto frame = proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
    bool ok = bot_ws_send_bytes(frame);
    if (ok) record_sent_message(group, "[图片] " + url);
    return ok;
}

bool YuanbaoServer::send_group_file(const std::string& group, const std::string& url,
                                    const std::string& file_name,
                                    const std::string& uuid, int file_size,
                                    const std::string& at_user, const std::string& at_nick) {
    if (group.empty() || url.empty()) return false;
    // 与 Python send_file → _build_file_msg / _build_at_message 一致
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body;
    bytes_append(body, proto::encode_string(1, body_msg_id));
    bytes_append(body, proto::encode_string(2, group));
    bytes_append(body, proto::encode_string(3, config_.yuanbao_id));
    bytes_append(body, proto::encode_string(4, ""));
    uint32_t rnd = (uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000);
    bytes_append(body, proto::encode_string(5, std::to_string(rnd)));  // string 类型
    if (!at_user.empty())
        bytes_append(body, proto::encode_message(6, proto::encode_at_custom_elem(at_user, at_nick)));
    bytes_append(body, proto::encode_message(6, proto::encode_file_elem(url, uuid, file_size, file_name)));
    bytes_append(body, proto::encode_string(7, ""));
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    auto frame = proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
    bool ok = bot_ws_send_bytes(frame);
    if (ok) record_sent_message(group, "[文件] " + file_name);
    return ok;
}

bool YuanbaoServer::send_group_at(const std::string& group, const std::string& text,
                                  const std::string& user_id, const std::string& display_name) {
    // ← 修复：允许空文本（前端「@昵称」不带内容也可发送）
    if (group.empty() || user_id.empty()) return false;
    // 与 Python _build_at_message 完全一致：at_elem + text_elem（两个 field 6）
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_send_group_at_msg(
        body_msg_id, group, config_.yuanbao_id, text, user_id, display_name);
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    auto frame = proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
    bool ok = bot_ws_send_bytes(frame);
    // ← 修复：记录时补上 @ 前缀（对齐收到消息的 "@昵称 文本" 格式），
    //   否则自己发送的 @ 消息在消息面板中不显示 @
    if (ok) {
        std::string at_display = display_name.empty() ? user_id : display_name;
        record_sent_message(group, "@" + at_display + " " + text);
    }
    return ok;
}

bool YuanbaoServer::send_group_multi_at(const std::string& group, const std::string& text,
                                        const std::vector<std::pair<std::string, std::string>>& at_users) {
    if (group.empty() || at_users.empty()) return false;
    // 与 Python _build_multi_at_message 一致：多个 at_elem + text_elem
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_send_group_multi_at_msg(
        body_msg_id, group, config_.yuanbao_id, text, at_users);
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    auto frame = proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
    bool ok = bot_ws_send_bytes(frame);
    // ← 修复：记录时补上所有 @ 前缀，与收到的批量 @ 消息显示格式一致
    if (ok) {
        std::string ats;
        for (auto& u : at_users) {
            ats += "@" + (u.second.empty() ? u.first : u.second) + " ";
        }
        record_sent_message(group, ats + text);
    }
    return ok;
}

bool YuanbaoServer::send_group_parts(const std::string& group, const std::vector<proto::SendPart>& parts) {
    if (group.empty() || parts.empty()) return false;
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_send_group_parts_msg(
        body_msg_id, group, config_.yuanbao_id, parts);
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    auto frame = proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
    bool ok = bot_ws_send_bytes(frame);
    // 记录日志：按片段顺序还原 "@昵称" 与文本，保持原始位置
    if (ok) {
        std::string log;
        for (auto& p : parts) {
            if (p.type == 1)
                log += "@" + (p.display.empty() ? p.user_id : p.display);
            else
                log += p.text;
        }
        record_sent_message(group, log);
    }
    return ok;
}

void YuanbaoServer::bot_ws_recv_loop() {
    try {
    std::vector<char> buf(32768, 0);
    size_t buf_pos = 0;
    int64_t last_ping = util::now_ms();

    while (bot_running_.load() && bot_ws_fd_ >= 0) {
        // 对齐 openclaw：应用层心跳默认每 5 秒一次（服务端 PingRsp 可再调节）
        if (util::now_ms() - last_ping > 5000) {
            auto ping_frame = bot_ws_build_ping();
            bot_ws_send_bytes(ping_frame);
            last_ping = util::now_ms();
        }

#ifndef YUANBAO_NO_OPENSSL
        if (bot_ws_ssl_) {
            // 用 select 等待数据，避免阻塞
            { fd_set rfds; FD_ZERO(&rfds); FD_SET((unsigned)bot_ws_fd_, &rfds);
              struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
              if (select(bot_ws_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue; }
            
            int n;
            {
                // 与发送线程串行化，避免同一 SSL 对象并发读写
                std::lock_guard<std::mutex> _lk(bot_send_mu_);
                n = SSL_read(bot_ws_ssl_, buf.data() + buf_pos, (int)(buf.size() - buf_pos) - 1);
            }
            if (n <= 0) {
                if (n == 0) { std::cerr << "[Bot] SSL 关闭\n"; bot_connected_ = false; break; }
                int e = SSL_get_error(bot_ws_ssl_, n);
                if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
                if (e == SSL_ERROR_ZERO_RETURN) { std::cerr << "[Bot] SSL 零返回\n"; bot_connected_ = false; break; }
                std::cerr << "[Bot] SSL 错误 " << e << "\n"; bot_connected_ = false; break;
            }
            buf_pos += (size_t)n;

            // 解析所有完整帧
            size_t pos = 0;
            while (pos < buf_pos) {
                size_t consumed = 0;
                proto::WSFrame frame = proto::parse_ws_frame((uint8_t*)buf.data() + pos, buf_pos - pos, consumed);
                if (consumed == 0) break;
                pos += consumed;
                if (frame.opcode == 8) {  // close frame
                    std::cerr << "[Bot] 收到 close 帧, payload=" << frame.payload.size() << "\n";
                    // 回应 close
                    Bytes close_resp = proto::build_ws_frame(8, frame.payload, true);
                    bot_ws_send_bytes(close_resp);
                    bot_connected_ = false;
                    break;
                }
                if (frame.opcode == 9) {  // ping → pong
                    Bytes pong = proto::build_ws_frame(10, frame.payload, true);
                    bot_ws_send_bytes(pong);
                    continue;
                }
                if (!frame.payload.empty()) { bot_handle_frame(frame.payload); last_ping = util::now_ms(); }
            }
            if (!bot_connected_.load()) break;  // close frame 处理后退出
            if (pos > 0) {
                if (pos < buf_pos) memmove(buf.data(), buf.data() + pos, buf_pos - pos);
                buf_pos -= pos;
            }
            if (buf_pos >= buf.size() - 2048) buf_pos = 0;
            continue;
        }
#endif

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET((unsigned int)bot_ws_fd_, &rfds);
        struct timeval tv2; tv2.tv_sec = 5; tv2.tv_usec = 0;
        int sel = select(bot_ws_fd_ + 1, &rfds, nullptr, nullptr, &tv2);
        if (sel <= 0) continue;

        if (buf_pos >= buf.size()) buf_pos = 0;
        int n = recv(bot_ws_fd_, buf.data() + buf_pos, (int)(buf.size() - buf_pos), 0);
        if (n <= 0) {
            std::cerr << "[Bot] 连接断开，尝试重连...\n";
            bot_connected_ = false;
            close_socket(bot_ws_fd_); bot_ws_fd_ = -1;
            for (int i = 0; i < 3 && bot_running_.load(); i++) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (bot_ws_connect()) {
                    bot_ws_send_bytes(bot_ws_build_auth());
                    last_ping = util::now_ms();
                    break;
                }
            }
            break;
        }

        // ← 修复死循环：parse_ws_frame 内部会重置 pos（它把 pos 当纯输出），
        //   旧代码直接复用 pos 变量导致每轮都从缓冲区开头解析同一帧，永不推进。
        //   改用 consumed 输出参数 + 显式偏移，与上方 SSL 分支一致，正确逐帧消费。
        size_t pos = 0;
        while (pos < (size_t)n) {
            size_t consumed = 0;
            proto::WSFrame frame = proto::parse_ws_frame((uint8_t*)buf.data() + buf_pos + pos, (size_t)n - pos, consumed);
            if (consumed == 0) break;  // 数据不足，等待更多数据
            pos += consumed;
            if (!frame.payload.empty()) {
                bot_handle_frame(frame.payload);
                last_ping = util::now_ms();
            }
        }
        buf_pos = 0;
    }
    bot_connected_ = false;
    } catch (const std::exception& e) {
        std::cerr << "[Bot] recv_loop 异常: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[Bot] recv_loop 未知异常\n";
    }
}


void YuanbaoServer::bot_handle_frame(const Bytes& data) {
    if (data.empty()) return;

    // 1. 解码外层 ConnMsg（Protobuf）
    proto::DecodedMsg msg = proto::decode_conn_msg(data);
    
    // 如果 ConnMsg 解码失败，直接忽略
    if (msg.cmd.empty()) {
        std::cerr << "[Bot] RECV 解码失败, cmd 为空\n";
        return;
    }

    // Push 要求 ACK 时必须立即回 PushAck（cmd_type=3，复用原 head 的 cmd/module/msg_id），
    // 否则服务端会重推甚至判定客户端异常（对齐 openclaw 的 buildPushAck）
    if (msg.cmd_type == 2 && msg.need_ack) {
        Bytes ack_head = proto::encode_conn_head(3, msg.cmd, ++bot_seq_no_, msg.msg_id, msg.module);
        bot_ws_send_bytes(proto::build_ws_frame(2, proto::encode_conn_msg(ack_head)));
    }
    
    // 移除高频 hex 转储：每帧写 stderr（含成员列表 3KB 字节流）在 5 秒轮询下
    // 造成严重 IO 开销并放大卡顿，仅保留一次带 seq 的简要摘要供排查
    if (msg.cmd_type == 2) {
        // PUSH 消息（群消息等）也走此处，同样仅摘要
    }
    
    // 2. 根据 cmd_type 处理（与 Python sender.py 完全一致）
    int cmd_type = msg.cmd_type;  // 0=REQUEST, 1=RESPONSE, 2=PUSH
    std::string& cmd = msg.cmd;
    
    // Pong 响应
    if (cmd == "pong") return;

    // 应用层心跳：cmd_type==1 是服务端对我们 ping 的响应（可解析 heart_interval）；
    // 其余按服务端主动 ping 处理，回 pong 保活
    if (cmd == "ping") {
        if (cmd_type == 1) return;
        Bytes pong_head = proto::encode_conn_head(1, "pong", ++bot_seq_no_, util::random_hex_id(), "conn_access");
        bot_ws_send_bytes(proto::build_ws_frame(2, proto::encode_conn_msg(pong_head)));
        return;
    }
    
    // Auth-bind 响应 → 校验 code 后发 sync
    if (cmd == "auth-bind") {
        int auth_code = 0;
        if (!msg.data.empty()) {
            auto af = proto::decode_pb_message(msg.data.data(), msg.data.size());
            auto* cf = proto::find_field(af, 1);
            if (cf) auth_code = (int)cf->varint_val;
        }
        if (auth_code != 0) {
            std::cerr << "[Bot] 鉴权被拒绝 code=" << auth_code << "（不进入在线状态）\n";
            auth_response_received_ = false;
            return;
        }
        auth_response_received_ = true;
        if (!msg.data.empty()) {
            // auth 数据为 token/IP 等 ASCII 文本，只保留可打印 ASCII，彻底避免乱码
            std::string ds(msg.data.begin(), msg.data.end());
            std::string clean;
            for (char ch : ds) {
                unsigned char uc = (unsigned char)ch;
                if (uc >= 32 && uc <= 126) clean += ch;
                else clean += '.';
            }
            if (clean.size() > 120) clean = clean.substr(0, 120);
            std::cerr << "[Bot] Auth: " << clean << "\n";
        }
        if (!sync_sent_) {
            sync_sent_ = true;
            bot_ws_send_bytes(bot_ws_build_sync_info());
            std::cerr << "[Bot] 命令列表已同步到平台（内置 "
                      << (sizeof(kSlashCommands) / sizeof(kSlashCommands[0])) << " + 插件 "
                      << (sizeof(kPluginCommands) / sizeof(kPluginCommands[0])) << " 个命令）\n";
        }
        return;
    }

    // SyncInformation 响应 → 打印命令同步结果（命令菜单能否出现看这里）
    if (cmd == "sync_information") {
        // protobuf 里 code=0 是默认值、编码时会被省略 → 未读到 code 字段即视为成功
        int code = 0;
        std::string smsg;
        if (!msg.data.empty()) {
            auto fields = proto::decode_pb_message(msg.data.data(), msg.data.size());
            if (auto* cf = proto::find_field(fields, 1)) code = (int)cf->varint_val;
            if (auto* sf = proto::find_field(fields, 2))
                smsg = !sf->str_val.empty() ? sf->str_val
                                            : std::string(sf->bytes_val.begin(), sf->bytes_val.end());
        }
        if (msg.data.empty()) std::cerr << "[Bot] 命令列表已同步（平台未返回明细）\n";
        else if (code == 0) std::cerr << "[Bot] 命令列表同步成功（平台已注册命令菜单）\n";
        else std::cerr << "[Bot] 命令列表同步失败 code=" << code
                       << (smsg.empty() ? "" : (" msg=" + smsg)) << "\n";
        return;
    }

    // 回复状态心跳响应（send_group_heartbeat / send_private_heartbeat）：
    // 平台拒绝时打印原因（气泡收不掉时看这里），成功静默
    if ((cmd == "send_group_heartbeat" || cmd == "send_private_heartbeat") && cmd_type == 1) {
        if (!msg.data.empty()) {
            // 注意：protobuf 的 code=0 不会写进编码结果，成功响应通常只有 msg="succ"，
            // 因此未读到 code 字段时必须按成功处理（此前默认 -1 会把成功误报为"被平台拒绝"）
            int code = 0;
            std::string hmsg;
            auto fields = proto::decode_pb_message(msg.data.data(), msg.data.size());
            if (auto* cf = proto::find_field(fields, 1)) code = (int)cf->varint_val;
            if (auto* sf = proto::find_field(fields, 2))
                hmsg = !sf->str_val.empty() ? sf->str_val
                                            : std::string(sf->bytes_val.begin(), sf->bytes_val.end());
            if (code != 0)
                std::cerr << "[气泡] 心跳被平台拒绝 code=" << code
                          << (hmsg.empty() ? "" : (" msg=" + hmsg)) << "\n";
        }
        return;
    }
    
    // GetGroupMemberList 响应 → 解析并缓存成员
    if (cmd == "get_group_member_list" && cmd_type == 1) {
        std::vector<std::pair<std::string, std::string>> members;
        auto fields = proto::decode_pb_message(msg.data.data(), msg.data.size());
        for (auto& f : fields) {
            if (f.fn == 3 && !f.sub_fields.empty()) {  // repeated Member
                std::string uid, nick;
                for (auto& sf : f.sub_fields) {
                    if (sf.fn == 1) uid = sf.str_val.empty() ? std::string(sf.bytes_val.begin(), sf.bytes_val.end()) : sf.str_val;
                    if (sf.fn == 2) nick = sf.str_val.empty() ? std::string(sf.bytes_val.begin(), sf.bytes_val.end()) : sf.str_val;
                }
                if (!uid.empty()) members.push_back({uid, nick});
            }
        }
        {
            std::lock_guard<std::mutex> l(members_mu_);
            // ← 修复：按群分别缓存（切换派后不同群不互相串缓存）
            std::string g = members_request_group_;
            if (g.empty()) g = config_.default_target();
            auto& entry = members_cache_map_[g];
            entry.members = std::move(members);
            entry.fetched_at = util::now_ms();
            members_response_ = true;
            members_fetched_at_ = entry.fetched_at;
        }
        members_cv_.notify_all();
        std::lock_guard<std::mutex> l(members_mu_);
        std::cerr << "[Bot] get_group_member_list 响应(" << members_request_group_
                  << "): " << members_cache_map_[members_request_group_].members.size() << " 人\n";
        return;
    }

    // QueryGroupInfo 响应 → 解析群名与群主 user_id（与 Python _decode_query_group_info_rsp 一致）
    if (cmd == "query_group_info" && cmd_type == 1) {
        std::string owner_id;
        std::string group_name;
        auto fields = proto::decode_pb_message(msg.data.data(), msg.data.size());
        for (auto& f : fields) {
            if (f.fn == 3 && !f.sub_fields.empty()) {  // GroupInfo 嵌套消息
                for (auto& sf : f.sub_fields) {
                    if (sf.fn == 1)  // group_name
                        group_name = sf.str_val.empty() ? std::string(sf.bytes_val.begin(), sf.bytes_val.end()) : sf.str_val;
                    else if (sf.fn == 2)  // group_owner_user_id
                        owner_id = sf.str_val.empty() ? std::string(sf.bytes_val.begin(), sf.bytes_val.end()) : sf.str_val;
                }
            }
        }
        // ← 修复竞态：按 msg_id 关联到发送时登记的群号（多群并发查询不再张冠李戴）
        std::string target_group;
        bool matched = false;
        {
            std::lock_guard<std::mutex> l(members_mu_);
            auto pit = pending_query_groups_.find(msg.msg_id);
            if (pit != pending_query_groups_.end()) {
                target_group = pit->second;
                pending_query_groups_.erase(pit);
                matched = true;
            }
        }
        // ← 主群概念已移除：查询目标即默认目标群，响应直接更新群信息
        {
            std::lock_guard<std::mutex> l(members_mu_);
            if (matched) {
                // 按群更新 owner/name（写入该群的成员缓存条目，群信息与成员同群）
                auto& entry = members_cache_map_[target_group];
                if (!owner_id.empty()) entry.owner_id = owner_id;
                if (!group_name.empty()) entry.group_name = group_name;
                // 兼容旧引用：默认目标群的 owner/name 同步到旧字段
                if (target_group == config_.default_target()) {
                    if (!owner_id.empty()) members_owner_id_ = owner_id;
                    if (!group_name.empty()) members_group_name_ = group_name;
                }
            }
            if (matched && !group_name.empty())
                known_groups_[target_group] = group_name;
            group_info_response_ = true;
        }
        members_cv_.notify_all();
        if (matched) persist_known_groups();   // ← 群名更新后落盘
        std::cerr << "[Bot] query_group_info 响应" << (matched ? " (matched=" + target_group + ")" : " (未匹配)")
                  << ": name=" << (group_name.empty() ? "(无)" : group_name)
                  << " owner=" << (owner_id.empty() ? "(无)" : owner_id.substr(0, 24)) << "\n";
        return;
    }
    
    // 3. PUSH 消息（群消息/C2C 消息推送）
    //    真实协议为 protobuf：外层可能是 PushMsg 包装，内层为 InboundMessagePush；
    //    同时保留旧 JSON 解析作为回退。
    if (cmd_type == 2 && !msg.data.empty()) {
        // 3.1 尝试解开 PushMsg 包装（1=cmd 2=module 3=msg_id 4=data）
        Bytes biz = msg.data;
        {
            auto pf = proto::decode_pb_message(msg.data.data(), msg.data.size());
            auto* pc = proto::find_field(pf, 1);
            auto* pm = proto::find_field(pf, 2);
            auto* pd = proto::find_field(pf, 4);
            if (pc && !pc->bytes_val.empty() && pm && !pm->bytes_val.empty()
                && pd && !pd->bytes_val.empty()) {
                biz = pd->bytes_val;
            }
        }

        JsonVal push_json;
        bool parsed = false;

        // 3.2 优先按 InboundMessagePush protobuf 解析
        {
            proto::InboundMsg im = proto::parse_inbound_msg(biz);
            if (!im.from_account.empty() || !im.callback_command.empty()
                || !im.body.empty() || !im.group_code.empty() || !im.msg_id.empty()) {
                push_json["callback_command"] = im.callback_command;
                push_json["from_account"] = im.from_account;
                push_json["to_account"] = im.to_account;
                push_json["sender_nickname"] = im.sender_nickname;
                push_json["group_code"] = im.group_code;
                push_json["group_name"] = im.group_name;
                push_json["msg_id"] = im.msg_id;
                push_json["msg_seq"] = (double)im.msg_seq;
                if (im.claw_msg_type != 0) push_json["claw_msg_type"] = (double)im.claw_msg_type;
                for (auto& bf : im.body) {
                    auto ef = proto::decode_pb_message(bf.bytes_val.data(), bf.bytes_val.size());
                    JsonVal elem;
                    JsonVal mc;
                    for (auto& sf : ef) {
                        if (sf.fn == 1) {
                            elem["msg_type"] = proto::pb_field_str(sf);
                        } else if (sf.fn == 2) {
                            auto cf = proto::decode_pb_message(sf.bytes_val.data(), sf.bytes_val.size());
                            for (auto& c : cf) {
                                switch (c.fn) {
                                    case 1:  mc["text"] = proto::pb_field_str(c); break;
                                    case 2:  mc["uuid"] = proto::pb_field_str(c); break;
                                    case 3:  mc["image_format"] = (double)c.varint_val; break;
                                    case 4:  mc["data"] = proto::pb_field_str(c); break;
                                    case 5:  mc["desc"] = proto::pb_field_str(c); break;
                                    case 8: {
                                        JsonVal ii;
                                        auto ic = proto::decode_pb_message(c.bytes_val.data(), c.bytes_val.size());
                                        for (auto& x : ic) {
                                            if (x.fn == 1) ii["type"] = (double)x.varint_val;
                                            else if (x.fn == 2) ii["size"] = (double)x.varint_val;
                                            else if (x.fn == 3) ii["width"] = (double)x.varint_val;
                                            else if (x.fn == 4) ii["height"] = (double)x.varint_val;
                                            else if (x.fn == 5) ii["url"] = proto::pb_field_str(x);
                                        }
                                        mc["image_info_array"].arr.push_back(ii);
                                        break;
                                    }
                                    case 10: mc["url"] = proto::pb_field_str(c); break;
                                    case 11: mc["file_size"] = (double)c.varint_val; break;
                                    case 12: mc["file_name"] = proto::pb_field_str(c); break;
                                    default: break;
                                }
                            }
                        }
                    }
                    elem["msg_content"] = mc;
                    push_json["msg_body"].arr.push_back(elem);
                }
                parsed = true;
            }
        }

        // 3.3 回退 JSON（兼容旧协议/旧数据）
        if (!parsed) {
            std::string json_str(biz.begin(), biz.end());
            parsed = json_parse(json_str, push_json);
            if (!parsed) {
                std::cerr << "[Bot] PUSH 解析失败(protobuf/json): "
                          << json_str.substr(0, 80) << "\n";
                return;
            }
        }
        
        // 提取基本字段（与 Python sender.py 一致）
        std::string sender_name = push_json["sender_nickname"].asString();
        std::string sender_id   = push_json["from_account"].asString();
        std::string group_code  = push_json["group_code"].asString();
        std::string msg_id_s    = push_json["msg_id"].asString();

        // ← 修复：撤回事件通知（对齐 Python group_monitor.py 的 Group.CallbackAfterRecallMsg）
        //   兼容多种回调命令字段名与值，并支持缺失回调字段但含撤回列表结构的兜底判定
        bool is_recall = false;
        std::string cb = push_json["callback_command"].asString();
        if (cb.empty()) cb = push_json["CallbackCommand"].asString();
        if (cb == "Group.CallbackAfterRecallMsg" || cb == "Group.CallbackAfterRecallMsgEx"
            || cb == "Group.CallbackRecallMsg") {
            is_recall = true;
        } else if (cb.empty()
                   && (!push_json["recall_msg_seq_list"].arr.empty() || !push_json["MsgSeqList"].arr.empty())
                   && !push_json["group_code"].asString().empty()) {
            // 无回调命令字段但结构上确实是撤回事件（含撤回列表 + 群号）
            is_recall = true;
        }
        if (is_recall) {
            // 撤回监控功能已移除：撤回事件不再发送通知，直接忽略
            return;
        }

        // ← 修复：多群聊监听。任何群消息都说明 bot 在该群，先把群号记入 known_groups_；
        //   群名查询统一由 /api/groups 串行处理（按 msg_id 关联），PUSH 不再发异步查询，
        //   避免 PUSH 查询与前端 /api/groups、/api/members 并发导致群名错乱
        if (!group_code.empty()) {
            bool is_new = false;
            {
                std::lock_guard<std::mutex> l(members_mu_);
                if (!known_groups_.count(group_code)) { known_groups_[group_code] = ""; is_new = true; }
            }
            if (is_new) persist_known_groups();   // ← 落盘，重启不丢失
            // 自动获取监听群：新发现的群自动加入监听列表（除非用户已手动关闭过，
            // 此时 listen_groups 非空且不含该群，保持用户选择）
            if (is_new && config_.listen_groups.empty()) {
                config_.listen_groups.push_back(group_code);
                save_config();
            }
        }
        // 监听范围：默认监听机器人所在的所有群（listen_all_groups_）；
        // 若用 listen --only-listen-groups 显式限制，则只处理 config.LISTEN_GROUPS 内的群
        if (!group_code.empty() && !listen_all_groups_.load() && !config_.is_listening(group_code)) {
            std::cerr << "[Bot] 忽略非监听群消息 (group=" << group_code << ")\n";
            return;
        }

        // 去重
        if (!msg_id_s.empty()) {
            std::lock_guard<std::mutex> l(seen_mu_);
            if (seen_msg_ids_.count(msg_id_s)) return;
            seen_msg_ids_.insert(msg_id_s);
            if (seen_msg_ids_.size() > 2000) {
                auto it = seen_msg_ids_.begin();
                for (int i = 0; i < 500 && it != seen_msg_ids_.end(); ++i, ++it);
                seen_msg_ids_.erase(seen_msg_ids_.begin(), it);
            }
        }
        
        // 解析 msg_body（与 Python 一致）
        std::string content;
        JsonVal media_info;
        if (!push_json["msg_body"].arr.empty()) {
            for (auto& elem : push_json["msg_body"].arr) {
                std::string mt = elem["msg_type"].asString();
                if (mt == "TIMTextElem") {
                    std::string txt = elem["msg_content"]["text"].asString();
                    if (!txt.empty()) content += txt;
                } else if (mt == "TIMCustomElem") {
                    // ← 修复：对齐 Python _extract_text 的自定义元素处理。
                    //   elem_type==1002 是 @消息（取 text）；其他类型取 text/content/tips；
                    //   data 解析失败时追加原始 data 字符串——原实现直接丢弃，
                    //   导致含自定义元素的消息内容从中间丢失（"从中间被截断"）。
                    std::string ds = elem["msg_content"]["data"].asString();
                    JsonVal cd;
                    if (!ds.empty() && json_parse(ds, cd) && !cd.obj.empty()) {
                        if (cd["elem_type"].asInt() == 1002) {
                            std::string t = cd["text"].asString();
                            if (!t.empty()) content += t + " ";
                        } else {
                            std::string t = cd["text"].asString();
                            if (t.empty()) t = cd["content"].asString();
                            if (t.empty()) t = cd["tips"].asString();
                            if (!t.empty()) content += t + " ";
                        }
                    } else if (!ds.empty()) {
                        // 解析失败：保留原始 data，避免内容丢失
                        content += ds + " ";
                    }
                } else if (mt == "TIMFaceElem") {
                    // ← 补：解析贴纸 media_info（对齐 Python _extract_media_info）
                    //   贴纸信息在 msg_content.data 内嵌 JSON：{sticker_id, name, package_id}
                    std::string face_name;
                    std::string fd_str = elem["msg_content"]["data"].asString();
                    if (!fd_str.empty()) {
                        JsonVal fd;
                        if (json_parse(fd_str, fd)) {
                            face_name = fd["name"].asString();
                            if (face_name.empty()) face_name = fd["face_name"].asString();
                            media_info["type"] = "sticker";
                            media_info["sticker_id"] = fd["sticker_id"].asString();
                            media_info["sticker_name"] = face_name;
                            media_info["package_id"] = fd["package_id"].asString();
                        }
                    }
                    if (face_name.empty()) face_name = elem["msg_content"]["name"].asString();
                    content += face_name.empty() ? "[贴纸]" : ("[贴纸:" + face_name + "]");
                } else if (mt == "TIMImageElem") {
                    content += "[图片]";
                    // ← 补：解析图片媒体信息，前端可渲染缩略图（对齐 Python media_info）
                    std::string url;
                    int iw = 0, ih = 0, isz = 0;
                    for (auto& ii : elem["msg_content"]["image_info_array"].arr) {
                        std::string u = ii["url"].asString();
                        if (!u.empty()) {
                            url = u;
                            iw = ii["width"].asInt();
                            ih = ii["height"].asInt();
                            isz = ii["size"].asInt();
                        }
                    }
                    if (!url.empty()) {
                        JsonVal urls;
                        JsonVal uv; uv.v = url;
                        urls.arr.push_back(uv);
                        media_info["type"] = "image";
                        media_info["image_urls"] = urls;
                        media_info["image_uuid"] = elem["msg_content"]["uuid"].asString();
                        media_info["image_width"] = iw;
                        media_info["image_height"] = ih;
                        media_info["image_size"] = isz;
                    }
                } else if (mt == "TIMFileElem") {
                    // ← 补：解析文件 media_info（对齐 Python _extract_media_info）
                    std::string fn = elem["msg_content"]["file_name"].asString();
                    media_info["type"] = "file";
                    media_info["file_url"] = elem["msg_content"]["url"].asString();
                    media_info["file_name"] = fn;
                    media_info["file_uuid"] = elem["msg_content"]["uuid"].asString();
                    media_info["file_size"] = elem["msg_content"]["file_size"].asInt();
                    content += fn.empty() ? "[文件]" : ("[文件:" + fn + "]");
                } else if (mt == "TIMVideoFileElem") {
                    // ← 补：解析视频 media_info（对齐 Python _extract_media_info）
                    std::string vn = elem["msg_content"]["file_name"].asString();
                    media_info["type"] = "video";
                    media_info["url"] = elem["msg_content"]["url"].asString();
                    media_info["uuid"] = elem["msg_content"]["uuid"].asString();
                    media_info["size"] = elem["msg_content"]["file_size"].asInt();
                    media_info["name"] = vn;
                    content += vn.empty() ? "[视频]" : ("[视频:" + vn + "]");
                }
            }
        }
        
        // 代理转发功能已移除
        std::cerr << "[Bot] 收到消息: " << sender_name << "(" << sender_id 
                  << ") in " << (group_code.empty() ? "(私聊)" : group_code)
                  << " to_account=" << push_json["to_account"].asString()
                  << " bot_id=" << config_.yuanbao_id
                  << ": " << content.substr(0, 80) << "\n";

        // AI 上下文记忆：记录该群/私聊的所有消息（不论是否 @ 机器人）
        if (!content.empty()) {
            bool msg_is_c2c = group_code.empty();
            ai_context_append(false, msg_is_c2c,
                              group_code, msg_is_c2c ? sender_id : std::string(),
                              sender_id, sender_name, content);
        }
        
        JsonVal log_entry;
        std::string ts_str = util::time_iso();
        log_entry["msg_id"] = msg_id_s;
        // ← 修复：缓存 msg_seq（撤回匹配关键字段；兼容大写 MsgSeq，如腾讯云标准）
        int msg_seq = push_json["msg_seq"].asInt();
        if (msg_seq <= 0) msg_seq = push_json["MsgSeq"].asInt();
        if (msg_seq > 0) log_entry["msg_seq"] = msg_seq;
        log_entry["timestamp"] = ts_str;   // 日志用
        log_entry["time"] = ts_str;        // 前端 renderMessages 读 m.time
        log_entry["cmd"] = cmd; log_entry["sender_name"] = sender_name;
        log_entry["sender_id"] = sender_id; log_entry["group_code"] = group_code;
        if (!config_.account_name.empty()) log_entry["account"] = config_.account_name;
        log_entry["content"] = content;
        // ← 修复：标识私聊消息（group_code 为空 = C2C 私聊），记录对方 ID 供前端按人过滤历史
        bool is_c2c = group_code.empty();
        if (is_c2c) { log_entry["is_c2c"] = true; log_entry["c2c_peer"] = sender_id; }
        if (!media_info.obj.empty()) log_entry["media_info"] = media_info;
        msg_logger_.log(log_entry); cache_message(log_entry);

        // ── 被 @ 或私聊消息 → 启动 RUNNING 心跳，点亮元宝"加载中/思考中"气泡 ──
        //   判定对齐官方插件（business/messaging/handlers/custom）：@ 元素(elem_type=1002) 的
        //   user_id 必须等于本 bot 的 id 才算被 @；**不再按昵称文本匹配**
        //   —— 否则群里 @ 另一个也叫"元宝"的机器人（甚至昵称相同的普通成员）都会被误触发
        bool mentioned = false;
        std::string mention_src;
        std::string bot_name = bot_display_name();
        bool saw_at_elem = false, uid_available = false;
        std::string nick_fallback;   // 仅当平台未提供任何 user_id 时才允许用昵称兜底
        for (auto& elem : push_json["msg_body"].arr) {
            if (elem["msg_type"].asString() != "TIMCustomElem") continue;
            JsonVal cd;
            if (!json_parse(elem["msg_content"]["data"].asString(), cd)) continue;
            if (cd["elem_type"].asInt() != 1002) continue;
            saw_at_elem = true;
            std::string at_uid = cd["user_id"].asString();
            std::string at_text = cd["text"].asString();
            if (!at_uid.empty()) uid_available = true;
            bool hit = (!config_.yuanbao_id.empty() && at_uid == config_.yuanbao_id);
            std::cout << "[@] " << (hit ? "命中本 bot" : "非本 bot")
                      << "  user_id=" << (at_uid.empty() ? "(空)" : at_uid)
                      << "  text=" << at_text
                      << "  bot_id=" << config_.yuanbao_id
                      << "  bot_name=" << bot_name << "\n";
            if (hit) { mentioned = true; mention_src = "@元素 user_id"; break; }
            // 只有该 @ 元素连 user_id 都没有时，才记录昵称文本作为兜底候选
            if (at_uid.empty() && !bot_name.empty() && at_text == "@" + bot_name)
                nick_fallback = at_text;
        }
        // 兜底（默认关闭）：平台未下发任何 user_id 且 config 打开 AT_NICKNAME_FALLBACK 时，
        // 才退回"@本bot昵称"文本判定（@ 同名机器人时对方 user_id 非空，不会走到这里）
        if (!mentioned && saw_at_elem && !uid_available && !nick_fallback.empty()) {
            if (config_.at_nickname_fallback) {
                mentioned = true;
                mention_src = "昵称文本兜底(平台未提供 user_id)";
            } else {
                std::cerr << "[@] 平台未下发 @ 元素 user_id，昵称兜底默认关闭；"
                             "若 @ 机器人无反应，可在 config.json 设 \"AT_NICKNAME_FALLBACK\": true\n";
            }
        }
        // 注意：不再用 to_account 判定 —— 群消息的 to_account 通常就是本 bot（消息接收者），
        // 用它会导致"群里任何消息都被当成被 @"（官方插件也只认 @ 元素的 user_id）
        if (mentioned)
            std::cout << "[Bot] 判定为被 @（来源: " << mention_src << "）\n";
        // ── 斜杠命令（/help /status /new /reset /stop /compact /retry）──
        //   群聊需 @ 机器人（对齐 yuanbao-openclaw-plugin 的 guard），私聊直接可用；
        //   命令不进入 AI 上下文，也不触发 AI 自动回复
        if (sender_id != config_.yuanbao_id) {
            std::string cmd_arg;
            std::string slash = parse_slash_command(content, cmd_arg);
            if (!slash.empty() && (is_c2c || mentioned)) {
                ai_context_drop_last(is_c2c, group_code, is_c2c ? sender_id : std::string(),
                                     sender_id, content);
                handle_slash_command(is_c2c, group_code, sender_id, sender_name, slash, cmd_arg);
                return;
            }
        }

        if (is_c2c || mentioned) {
            heartbeat_start_chat(is_c2c, group_code, sender_id);
        }

        // AI 自动回复（listen 默认启用，--no-ai 可关闭）：私聊直接回复；群聊被 @ 时回复
        if (ai_auto_reply_.load() && !content.empty() && sender_id != config_.yuanbao_id) {
            if (is_c2c || mentioned) {
                JsonVal ai_msg = log_entry;
                std::thread([this, ai_msg]() {
                    process_llm_reply(ai_msg, true);   // 已判定被 @ / 私聊 → 强制回复
                }).detach();
            }
        }
        return;
    }
    
    // 4. 其他消息（日志）
    if (!msg.data.empty()) {
        std::string ds(msg.data.begin(), msg.data.end());
        std::cerr << "[Bot] msg: cmd=" << cmd << " module=" << msg.module 
                  << " type=" << cmd_type << " data=" << ds.substr(0, 100) << "\n";
    }
}

// 临时覆盖 LLM 配置（命令行 --llm-* 参数；空值/负值忽略）
void YuanbaoServer::set_llm_override(const std::string& url, const std::string& key,
                                     const std::string& model, const std::string& system_prompt,
                                     int max_tokens, double temperature) {
    if (!url.empty()) config_.llm.api_url = url;
    if (!key.empty()) config_.llm.api_key = key;
    if (!model.empty()) config_.llm.model = model;
    if (!system_prompt.empty()) config_.llm.system_prompt = system_prompt;
    if (max_tokens > 0) config_.llm.max_tokens = max_tokens;
    if (temperature >= 0) config_.llm.temperature = temperature;
    config_.llm.enabled = !config_.llm.api_url.empty() && !config_.llm.api_key.empty();
}

// ═══════════════════════════════════════════════
//  回复状态心跳（RUNNING=1 / FINISH=2）：控制元宝"加载中/思考中"气泡
// ═══════════════════════════════════════════════
Bytes YuanbaoServer::bot_ws_build_group_heartbeat(const std::string& group, const std::string& to,
                                                  int heartbeat, uint64_t send_time_ms) {
    Bytes data = proto::encode_send_group_heartbeat(config_.yuanbao_id, to, group,
                                                    (int64_t)send_time_ms, heartbeat);
    Bytes head = proto::encode_conn_head(0, "send_group_heartbeat", ++bot_seq_no_,
                                         util::random_hex_id(), "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, data));
}

Bytes YuanbaoServer::bot_ws_build_private_heartbeat(const std::string& to, int heartbeat) {
    Bytes data = proto::encode_send_private_heartbeat(config_.yuanbao_id, to, heartbeat);
    Bytes head = proto::encode_conn_head(0, "send_private_heartbeat", ++bot_seq_no_,
                                         util::random_hex_id(), "yuanbao_openclaw_proxy");
    return proto::build_ws_frame(2, proto::encode_conn_msg(head, data));
}

bool YuanbaoServer::heartbeat_send(bool is_group, const std::string& group, const std::string& to,
                                   int heartbeat, uint64_t send_time_ms) {
    if (config_.yuanbao_id.empty() || to.empty() || bot_ws_fd_ < 0) return false;
    Bytes frame = is_group ? bot_ws_build_group_heartbeat(group, to, heartbeat, send_time_ms)
                           : bot_ws_build_private_heartbeat(to, heartbeat);
    return bot_ws_send_bytes(frame);
}

// 活跃会话落盘（logs/hb_sessions_<账号>.txt）：进程退出/被杀后，下次启动据此补发 FINISH，
// 避免元宝对话里"思考中"气泡永久残留
std::string YuanbaoServer::hb_sessions_path() const {
    std::string acct = config_.account_name.empty() ? std::string("default") : config_.account_name;
    return "logs/hb_sessions_" + acct + ".txt";
}

void YuanbaoServer::hb_persist_locked() {
    std::ofstream f(hb_sessions_path().c_str(), std::ios::trunc);
    if (!f.is_open()) return;
    for (auto& kv : hb_sessions_) {
        f << (kv.second->is_group ? "g" : "c") << "\t" << kv.second->group << "\t"
          << kv.second->to << "\n";
    }
}

void YuanbaoServer::heartbeat_start(bool is_group, const std::string& group, const std::string& to,
                                    int keepalive_sec, bool manual_hold) {
    if (to.empty() || (is_group && group.empty())) return;
    std::string key = is_group ? ("g:" + group + ":" + to) : ("c:" + to);
    uint64_t keep = keepalive_sec > 0 ? (uint64_t)keepalive_sec * 1000ULL : 30000ULL;
    {
        std::lock_guard<std::mutex> l(hb_mu_);
        auto it = hb_sessions_.find(key);
        if (it != hb_sessions_.end()) {
            it->second->stop = false;
            it->second->need_finish = false;   // 重新点亮 → 取消待补发，避免旧 FINISH 误收新气泡
            it->second->start_ms = util::now_ms();
            it->second->last_emit_ms = (int64_t)util::now_ms();
            it->second->keepalive_ms = keep;
            it->second->finish_at_ms = 0;   // 重新点亮 → 取消之前的延时收起
            it->second->manual_hold = manual_hold;
        } else {
            auto s = std::make_shared<HBSession>();
            s->is_group = is_group;
            s->group = group;
            s->to = to;
            s->start_ms = util::now_ms();
            s->last_emit_ms = (int64_t)s->start_ms;
            s->keepalive_ms = keep;
            s->manual_hold = manual_hold;
            hb_sessions_[key] = s;
        }
        hb_persist_locked();
        if (is_group) {
            // 记录活跃目标（供一次性的 send-* 命令定位 to_account 并发送 FINISH）
            std::string acct = config_.account_name.empty() ? std::string() : (config_.account_name + "_");
            std::ofstream f("logs/hb_active_" + acct + group);
            if (f.is_open()) { f << to; f.close(); }
        }
    }
    if (!hb_thread_started_.exchange(true)) {
        hb_thread_ = std::thread([this]() { heartbeat_loop(); });
    }
}

// 便捷包装：按会话类型发心跳（is_c2c=true → 私聊心跳；否则群心跳）
void YuanbaoServer::heartbeat_start_chat(bool is_c2c, const std::string& group, const std::string& peer,
                                         int keepalive_sec, bool manual_hold) {
    if (peer.empty()) return;
    if (is_c2c) heartbeat_start(false, "", peer, keepalive_sec, manual_hold);
    else if (!group.empty()) heartbeat_start(true, group, peer, keepalive_sec, manual_hold);
}

// 自动收尾：AI 用 [bubble:on] 手动保持时跳过（交给保活到期或 [bubble:off]）
void YuanbaoServer::heartbeat_finish_auto(bool is_c2c, const std::string& group, const std::string& peer) {
    if (peer.empty()) return;
    std::string key = is_c2c ? ("c:" + peer) : ("g:" + group + ":" + peer);
    {
        std::lock_guard<std::mutex> l(hb_mu_);
        auto it = hb_sessions_.find(key);
        if (it != hb_sessions_.end() && it->second->manual_hold) {
            std::cout << "[BUBBLE] AI 手动保持气泡中（剩 "
                      << (it->second->keepalive_ms / 1000) << "s 保活），跳过自动收起\n";
            return;
        }
    }
    heartbeat_finish_chat(is_c2c, group, peer);
}

void YuanbaoServer::heartbeat_finish_chat(bool is_c2c, const std::string& group, const std::string& peer) {
    if (peer.empty()) return;
    if (is_c2c) heartbeat_finish(false, "", peer);
    else if (!group.empty()) heartbeat_finish(true, group, peer);
}

// AI 控制气泡：统一入口（支持保活时长与延时收起）
void YuanbaoServer::ai_bubble_control(bool is_c2c, const std::string& group, const std::string& peer,
                                      bool on, int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 86400) seconds = 86400;   // 上限 24 小时
    if (on) {
        heartbeat_start_chat(is_c2c, group, peer, seconds, /*manual_hold=*/true);
        std::cout << "[BUBBLE] AI 控制气泡: RUNNING 保活 "
                  << (seconds > 0 ? seconds : 30) << "s\n";
        return;
    }
    if (seconds > 0) {   // 延时收起：写入 finish_at_ms，由 heartbeat_loop 到点执行（可被 on 取消）
        std::string key = is_c2c ? ("c:" + peer) : ("g:" + group + ":" + peer);
        std::lock_guard<std::mutex> l(hb_mu_);
        auto it = hb_sessions_.find(key);
        if (it != hb_sessions_.end()) {
            it->second->finish_at_ms = (int64_t)util::now_ms() + (int64_t)seconds * 1000;
        } else {
            // 尚未点亮：记录一个占位，到点发一次 FINISH（幂等）
            auto s = std::make_shared<HBSession>();
            s->is_group = !is_c2c;
            s->group = group;
            s->to = peer;
            s->start_ms = util::now_ms();
            s->last_emit_ms = (int64_t)s->start_ms;
            s->stop = true;   // 不续发 RUNNING
            s->finish_at_ms = (int64_t)util::now_ms() + (int64_t)seconds * 1000;
            hb_sessions_[key] = s;
        }
        std::cout << "[BUBBLE] AI 控制气泡: " << seconds << " 秒后 FINISH（可被 [bubble:on] 取消）\n";
        return;
    }
    heartbeat_finish_chat(is_c2c, group, peer);
    std::cout << "[BUBBLE] AI 控制气泡: FINISH\n";
}

void YuanbaoServer::heartbeat_finish(bool is_group, const std::string& group, const std::string& to) {
    if (to.empty()) return;
    std::string key = is_group ? ("g:" + group + ":" + to) : ("c:" + to);
    bool sent = heartbeat_send(is_group, group, to, 2, util::now_ms());   // FINISH
    std::cout << "[气泡] FINISH " << (is_group ? ("群 " + group) : ("私聊 " + to))
              << (sent ? " 已发送" : " 发送失败（连接不可用，稍后自动补发）") << "\n";
    std::lock_guard<std::mutex> l(hb_mu_);
    auto it = hb_sessions_.find(key);
    if (it == hb_sessions_.end()) { hb_persist_locked(); return; }
    it->second->stop = true;                 // 停止续发 RUNNING
    if (sent) {
        hb_sessions_.erase(it);              // 已收尾：从活跃表移除（落盘同步删除）
    } else {
        // 连接不可用（断线 / 正在退出）：保留记录，连接恢复后补发；进程重启时由启动清理补发
        it->second->need_finish = true;
        if (it->second->need_finish_since == 0) it->second->need_finish_since = util::now_ms();
    }
    hb_persist_locked();
}

void YuanbaoServer::heartbeat_loop() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!hb_thread_started_.load()) break;
        std::vector<std::shared_ptr<HBSession>> active, pending;
        {
            std::lock_guard<std::mutex> l(hb_mu_);
            for (auto& kv : hb_sessions_) {
                if (kv.second->need_finish) pending.push_back(kv.second);
                else if (!kv.second->stop) active.push_back(kv.second);
            }
        }
        uint64_t now = util::now_ms();

        // 0) 延时收起到点 → FINISH（可被后续 [bubble:on] 清零 finish_at_ms 取消）
        {
            std::vector<std::shared_ptr<HBSession>> due_finish;
            {
                std::lock_guard<std::mutex> l(hb_mu_);
                for (auto& kv : hb_sessions_)
                    if (kv.second->finish_at_ms > 0 && now >= (uint64_t)kv.second->finish_at_ms)
                        due_finish.push_back(kv.second);
            }
            for (auto& s : due_finish) {
                {
                    std::lock_guard<std::mutex> l(hb_mu_);
                    s->finish_at_ms = 0;   // 先清标记：发送失败时不会每 500ms 重复触发
                }
                heartbeat_finish(s->is_group, s->group, s->to);
                std::cout << "[BUBBLE] 延时收起已执行（"
                          << (s->is_group ? ("群 " + s->group) : ("私聊 " + s->to)) << "）\n";
            }
        }

        // 1) 连接曾断开 / 发送失败 → 补发未送达的 FINISH（即"恢复后气泡仍在"的修复点）
        for (auto& s : pending) {
            if (bot_ws_fd_ < 0) break;                     // 仍离线：等下一轮
            if (now - s->need_finish_since > 600000) {     // 10 分钟仍无法送达 → 放弃
                std::lock_guard<std::mutex> l(hb_mu_);
                hb_sessions_.erase(s->is_group ? ("g:" + s->group + ":" + s->to) : ("c:" + s->to));
                hb_persist_locked();
                continue;
            }
            if (heartbeat_send(s->is_group, s->group, s->to, 2, now)) {
                std::lock_guard<std::mutex> l(hb_mu_);
                hb_sessions_.erase(s->is_group ? ("g:" + s->group + ":" + s->to) : ("c:" + s->to));
                hb_persist_locked();
                std::cout << "[气泡] 连接已恢复，补发 FINISH → "
                          << (s->is_group ? s->group : ("私聊 " + s->to)) << "\n";
            }
        }

        // 2) 活跃会话：RUNNING 续发 + 30s 无收尾自动 FINISH
        for (auto& s : active) {
            // 跨进程停止信号：一次性 send-* 命令在发出 FINISH 后写入该文件
            std::string acct = config_.account_name.empty() ? std::string() : (config_.account_name + "_");
            std::string stop_file = "logs/hb_stop_" + acct + (s->is_group ? s->group + "_" : "") + s->to;
            std::ifstream sf(stop_file.c_str());
            if (sf.good()) {
                sf.close();
                std::remove(stop_file.c_str());
                heartbeat_finish(s->is_group, s->group, s->to);
                continue;
            }
            if (now - (uint64_t)s->last_emit_ms >= 2000) {
                heartbeat_send(s->is_group, s->group, s->to, 1, s->start_ms);  // RUNNING
                s->last_emit_ms = (int64_t)now;
            }
            if (now - s->start_ms > s->keepalive_ms) {   // 保活到期（默认 30s，可由 AI 指定）自动 FINISH
                heartbeat_finish(s->is_group, s->group, s->to);
            }
        }
    }
}

// 退出前收尾所有活跃气泡（listen 收到 Ctrl+C / 退出时调用）
void YuanbaoServer::heartbeat_finish_all() {
    std::vector<std::shared_ptr<HBSession>> all;
    {
        std::lock_guard<std::mutex> l(hb_mu_);
        for (auto& kv : hb_sessions_) all.push_back(kv.second);
    }
    for (auto& s : all) heartbeat_finish(s->is_group, s->group, s->to);
    if (!all.empty()) std::cout << "[气泡] 退出前收尾 " << all.size() << " 个思考中状态\n";
}

// 启动时清理上次进程残留的气泡：读 logs/hb_sessions_<账号>.txt，逐条补发 FINISH
int YuanbaoServer::heartbeat_cleanup_stale() {
    if (bot_ws_fd_ < 0) return 0;   // 尚未连接：保留记录，下次启动再清
    int n = 0;

    // 1) logs/hb_sessions_<账号>.txt：上次进程落盘的活跃会话（含被强杀的情况）
    {
        std::ifstream f(hb_sessions_path().c_str());
        if (f.is_open()) {
            std::vector<std::pair<bool, std::pair<std::string, std::string>>> items;
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t t1 = line.find('\t');
                if (t1 == std::string::npos) continue;
                size_t t2 = line.find('\t', t1 + 1);
                if (t2 == std::string::npos) continue;
                bool is_group = (line.substr(0, t1) == "g");
                std::string g = line.substr(t1 + 1, t2 - t1 - 1);
                std::string to = line.substr(t2 + 1);
                if (!to.empty()) items.push_back(std::make_pair(is_group, std::make_pair(g, to)));
            }
            f.close();
            for (auto& it : items) {
                if (heartbeat_send(it.first, it.second.first, it.second.second, 2, util::now_ms())) n++;
            }
            std::remove(hb_sessions_path().c_str());
        }
    }

    // 2) 旧机制兼容：logs/hb_active_<账号>_<群号>（老版本点亮的群气泡，文件内是最后被 @ 的用户）
    {
        std::string acct = config_.account_name.empty() ? std::string() : (config_.account_name + "_");
        std::string prefix = "hb_active_" + acct;
        std::vector<std::string> groups;
#ifdef YUANBAO_PLATFORM_WINDOWS
        std::wstring pat = util::utf8_to_wide("./logs/" + prefix) + L"*";
        WIN32_FIND_DATAW ffd;
        HANDLE h = FindFirstFileW(pat.c_str(), &ffd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::string fn = util::wide_to_utf8(std::wstring(ffd.cFileName));
                if (fn.size() > prefix.size()) groups.push_back(fn.substr(prefix.size()));
            } while (FindNextFileW(h, &ffd));
            FindClose(h);
        }
#else
        DIR* d = opendir("./logs");
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d))) {
                std::string fn = ent->d_name;
                if (fn.rfind(prefix, 0) == 0 && fn.size() > prefix.size())
                    groups.push_back(fn.substr(prefix.size()));
            }
            closedir(d);
        }
#endif
        for (auto& g : groups) {
            std::string p = "logs/" + prefix + g;
            std::string to;
            if (!util::read_file_utf8(p, to)) continue;
            while (!to.empty() && (to.back() == '\n' || to.back() == '\r' || to.back() == ' ')) to.pop_back();
            if (!to.empty() && heartbeat_send(true, g, to, 2, util::now_ms())) {
                n++;
                std::cout << "[气泡] 已清理遗留的思考中（群 " << g << "）\n";
            }
            std::remove(p.c_str());
        }
    }

    if (n > 0) std::cout << "[气泡] 共清理残留的思考中状态 " << n << " 条\n";
    return n;
}

void YuanbaoServer::process_llm_reply(const JsonVal& msg, bool force_reply) {
    std::string content = msg["content"].asString();
    std::string sender_id = msg["sender_id"].asString();
    std::string sender_name = msg["sender_name"].asString();
    std::string group_code = msg["group_code"].asString();
    bool is_c2c = msg["is_c2c"].asBool();

    if (content.empty()) return;
    if (sender_id == config_.yuanbao_id) return;

    // 判断是否需要回复（force_reply：调用方已确认被 @ / 私聊，直接回复）
    bool should_reply = force_reply;
    if (!should_reply) {
        if (is_c2c) {
            should_reply = true;  // 私聊总是回复
        } else {
            // 群聊兜底判定：仅文本明确 @ 本 bot 的 ID 才回复
            // （listen 主路径已由 @ 元素 user_id 精确判定并传 force_reply=true；
            //   不再匹配裸关键词"元宝/机器人"，避免同名机器人或闲聊误触发）
            should_reply = !config_.yuanbao_id.empty() &&
                           content.find("@" + config_.yuanbao_id) != std::string::npos;
        }
    }

    if (!should_reply) return;

    std::string session = ai_session_key(is_c2c, group_code, sender_id);
    if (ai_cancel_consume(session)) {   // 已被 /stop 取消 → 不再生成
        std::cout << "[LLM] 该会话已被 /stop 取消，跳过本次生成\n";
        heartbeat_finish_auto(is_c2c, group_code, sender_id);
        return;
    }

    // 使用大模型生成回复（含会话上下文 + 工具循环：命令/文件操作的结果会回填给模型继续推理）
    std::cout << "[LLM] 调用大模型生成回复（含会话上下文）...\n";
    ai_inflight_begin(session);
    std::string reply_text = ai_agent_reply(is_c2c, group_code, sender_id, content, sender_name, sender_id);
    ai_inflight_end(session);

    if (ai_cancel_consume(session)) {   // 生成期间收到 /stop → 丢弃本次回复
        std::cout << "[LLM] 生成期间收到 /stop，丢弃本次回复\n";
        heartbeat_finish_auto(is_c2c, group_code, sender_id);
        return;
    }
    if (reply_text.empty()) {
        std::cerr << "[LLM] 大模型返回空（已重试一次仍为空）\n";
        if (config_.llm.fallback_enabled && !config_.llm.fallback_text.empty()) {
            // 兜底回复：避免用户被 @/私聊后完全没有响应（对齐官方 fallbackReply）
            bool fok = send_ai_reply(is_c2c, group_code, sender_id, config_.llm.fallback_text,
                                     std::string(), std::string(), msg["msg_id"].asString(), sender_id);
            std::cout << "[LLM] 已发送兜底回复 (" << (fok ? "OK" : "FAIL") << ")"
                      << "，可用 LLM_FALLBACK_ENABLED=false 关闭\n";
        }
        heartbeat_finish_auto(is_c2c, group_code, sender_id);   // 收起"思考中"气泡
        return;
    }

    // 不再按 4500 字符硬截断：超长回复由 send_ai_reply 自动分条发送

    std::this_thread::sleep_for(std::chrono::milliseconds(300 + rand() % 500));
    // 富文本发送：解析 [sticker:名称] / @昵称，并按 REPLY_TO_MODE 引用触发本回复的消息
    bool ok = send_ai_reply(is_c2c, group_code, sender_id, reply_text,
                            std::string(), std::string(), msg["msg_id"].asString(), sender_id);
    std::cout << "[LLM] -> " << (is_c2c ? sender_id : group_code)
              << " reply: " << reply_text.substr(0, 80) << " " << (ok ? "OK" : "FAIL") << "\n";
    // 回复流程结束即收起"思考中"气泡（发送失败也收，否则气泡会一直残留）
    heartbeat_finish_auto(is_c2c, group_code, sender_id);
}

// ═══════════════════════════════════════════════
//  AI 会话上下文记忆
// ═══════════════════════════════════════════════

// 会话 → 落盘文件名（logs/ai_context/<sanitized>.json）
static std::string ai_ctx_file_of(const std::string& session) {
    std::string f;
    f.reserve(session.size());
    for (char c : session) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.') f += c;
        else f += '_';
    }
    return "logs/ai_context/" + f + ".json";
}

std::string YuanbaoServer::ai_session_key(bool is_c2c, const std::string& group, const std::string& peer) const {
    std::string acct = config_.account_name.empty() ? std::string("default") : config_.account_name;
    return is_c2c ? ("c_" + acct + "_" + peer) : ("g_" + acct + "_" + group);
}

void YuanbaoServer::ai_ctx_ensure_loaded_locked(const std::string& session) {
    if (ai_ctx_loaded_.count(session)) return;
    ai_ctx_loaded_.insert(session);
    std::string content;
    if (!util::read_file_utf8(ai_ctx_file_of(session), content)) return;
    JsonVal v;
    if (!json_parse(content, v)) return;
    std::deque<AiCtxMsg> dq;
    for (auto& it : v["messages"].arr) {
        AiCtxMsg m;
        m.sender_id = it["sender_id"].asString();
        m.sender_name = it["sender_name"].asString();
        m.content = it["content"].asString();
        m.is_bot = it["is_bot"].asBool();
        m.ts = (uint64_t)it["ts"].asDouble();
        if (m.content.empty()) continue;
        dq.push_back(std::move(m));
    }
    int limit = config_.ai_ctx_messages > 0 ? config_.ai_ctx_messages : 100;
    while ((int)dq.size() > limit) dq.pop_front();
    ai_ctx_[session] = std::move(dq);
}

void YuanbaoServer::ai_ctx_save_locked(const std::string& session) {
#ifdef _WIN32
    _mkdir("logs");
    _mkdir("logs/ai_context");
#else
    mkdir("logs", 0755);
    mkdir("logs/ai_context", 0755);
#endif
    auto it = ai_ctx_.find(session);
    if (it == ai_ctx_.end()) return;
    JsonVal out;
    for (auto& m : it->second) {
        JsonVal e;
        e["sender_id"] = m.sender_id;
        e["sender_name"] = m.sender_name;
        e["content"] = m.content;
        e["is_bot"] = m.is_bot;
        e["ts"] = (double)m.ts;
        out["messages"].arr.push_back(e);
    }
    std::ofstream f(ai_ctx_file_of(session));
    if (f.is_open()) { f << json_compact(out); f.close(); }
}

void YuanbaoServer::ai_context_append(bool is_bot, bool is_c2c, const std::string& group,
                                      const std::string& peer, const std::string& sender_id,
                                      const std::string& sender_name, const std::string& content) {
    if (!config_.ai_ctx_enabled || content.empty()) return;
    if (is_c2c ? peer.empty() : group.empty()) return;

    std::string session = ai_session_key(is_c2c, group, peer);
    std::lock_guard<std::mutex> l(ai_ctx_mu_);
    ai_ctx_ensure_loaded_locked(session);

    AiCtxMsg m;
    m.sender_id = sender_id;
    m.sender_name = sender_name;
    m.content = content;
    m.is_bot = is_bot;
    m.ts = util::now_ms();
    auto& dq = ai_ctx_[session];
    dq.push_back(std::move(m));
    int limit = config_.ai_ctx_messages > 0 ? config_.ai_ctx_messages : 100;
    while ((int)dq.size() > limit) dq.pop_front();
    ai_ctx_save_locked(session);
}

std::vector<YuanbaoServer::AiCtxMsg> YuanbaoServer::ai_ctx_snapshot(const std::string& session) {
    std::vector<AiCtxMsg> out;
    if (!config_.ai_ctx_enabled) return out;
    std::lock_guard<std::mutex> l(ai_ctx_mu_);
    ai_ctx_ensure_loaded_locked(session);
    auto it = ai_ctx_.find(session);
    if (it != ai_ctx_.end()) out.assign(it->second.begin(), it->second.end());
    return out;
}

// ═══════════════════════════════════════════════
//  斜杠命令支持（/help /status /new /reset /stop /compact /retry）
// ═══════════════════════════════════════════════

// /new、/reset：清空该会话上下文（内存 + 落盘文件）
void YuanbaoServer::ai_context_clear(bool is_c2c, const std::string& group, const std::string& peer) {
    std::string session = ai_session_key(is_c2c, group, peer);
    {
        std::lock_guard<std::mutex> l(ai_ctx_mu_);
        ai_ctx_.erase(session);
        ai_ctx_loaded_.insert(session);   // 标记已加载，避免再从旧文件回填
    }
    std::remove(ai_ctx_file_of(session).c_str());
}

int YuanbaoServer::ai_context_count(bool is_c2c, const std::string& group, const std::string& peer) {
    std::string session = ai_session_key(is_c2c, group, peer);
    std::lock_guard<std::mutex> l(ai_ctx_mu_);
    ai_ctx_ensure_loaded_locked(session);
    auto it = ai_ctx_.find(session);
    return it == ai_ctx_.end() ? 0 : (int)it->second.size();
}

// /compact：只保留最近一半（至少 10 条）；返回压缩前条数
int YuanbaoServer::ai_context_compact(bool is_c2c, const std::string& group, const std::string& peer) {
    std::string session = ai_session_key(is_c2c, group, peer);
    std::lock_guard<std::mutex> l(ai_ctx_mu_);
    ai_ctx_ensure_loaded_locked(session);
    auto it = ai_ctx_.find(session);
    if (it == ai_ctx_.end()) return 0;
    int before = (int)it->second.size();
    if (before <= 10) return before;
    int keep = before / 2;
    if (keep < 10) keep = 10;
    while ((int)it->second.size() > keep) it->second.pop_front();
    ai_ctx_save_locked(session);
    return before;
}

// 撤销刚记录的一条上下文（斜杠命令本身不进 AI 上下文）
void YuanbaoServer::ai_context_drop_last(bool is_c2c, const std::string& group, const std::string& peer,
                                         const std::string& sender_id, const std::string& content) {
    if (!config_.ai_ctx_enabled || content.empty()) return;
    if (is_c2c ? peer.empty() : group.empty()) return;
    std::string session = ai_session_key(is_c2c, group, peer);
    std::lock_guard<std::mutex> l(ai_ctx_mu_);
    ai_ctx_ensure_loaded_locked(session);
    auto it = ai_ctx_.find(session);
    if (it == ai_ctx_.end() || it->second.empty()) return;
    AiCtxMsg& last = it->second.back();
    if (last.is_bot || last.sender_id != sender_id || last.content != content) return;
    it->second.pop_back();
    ai_ctx_save_locked(session);
}

void YuanbaoServer::ai_inflight_begin(const std::string& session) {
    std::lock_guard<std::mutex> l(ai_cancel_mu_);
    ai_inflight_[session]++;
}

void YuanbaoServer::ai_inflight_end(const std::string& session) {
    std::lock_guard<std::mutex> l(ai_cancel_mu_);
    auto it = ai_inflight_.find(session);
    if (it == ai_inflight_.end()) return;
    if (--it->second <= 0) ai_inflight_.erase(it);
}

// /stop：仅当该会话确实有生成在跑时才置取消位（避免误吞下一条消息）
bool YuanbaoServer::ai_cancel_set_if_inflight(const std::string& session) {
    std::lock_guard<std::mutex> l(ai_cancel_mu_);
    auto it = ai_inflight_.find(session);
    if (it == ai_inflight_.end() || it->second <= 0) return false;
    ai_cancelled_.insert(session);
    return true;
}

bool YuanbaoServer::ai_cancel_consume(const std::string& session) {
    std::lock_guard<std::mutex> l(ai_cancel_mu_);
    return ai_cancelled_.erase(session) > 0;
}

// 斜杠命令分发
void YuanbaoServer::handle_slash_command(bool is_c2c, const std::string& group,
                                         const std::string& sender_id, const std::string& sender_name,
                                         const std::string& cmd, const std::string& arg) {
    (void)arg;
    auto reply = [&](const std::string& text) {
        std::cout << "[CMD] " << cmd << " → " << text.substr(0, 160) << "\n";
        if (is_c2c) send_c2c_text(sender_id, text);
        else if (!group.empty()) send_group_text(group, text);
    };
    std::cout << "[命令] " << cmd << " ← "
              << (is_c2c ? ("私聊 " + sender_id) : ("群 " + group))
              << " (" << sender_name << ")\n";

    if (cmd == "/help") {
        std::string text = "可用命令：";
        auto append_cmds = [&text](const SlashCommandDef* arr, size_t n) {
            for (size_t i = 0; i < n; i++) {
                text += "\n";
                text += arr[i].name;
                text += "  ";
                text += arr[i].desc;
            }
        };
        append_cmds(kSlashCommands, sizeof(kSlashCommands) / sizeof(kSlashCommands[0]));
        append_cmds(kPluginCommands, sizeof(kPluginCommands) / sizeof(kPluginCommands[0]));
        reply(text);
        return;
    }

    if (cmd == "/status") {
        std::ostringstream o;
        o << "机器人状态\n"
          << "账号: " << (config_.account_name.empty() ? "default" : config_.account_name) << "\n"
          << "连接: " << ((bot_connected_.load() && bot_ws_fd_ >= 0) ? "在线" : "离线") << "\n"
          << "AI 自动回复: " << (ai_auto_reply_.load() ? "开启" : "关闭") << "\n"
          << "本会话上下文: " << ai_context_count(is_c2c, group, sender_id) << " 条";
        reply(o.str());
        return;
    }

    if (cmd == "/new" || cmd == "/reset") {
        ai_context_clear(is_c2c, group, sender_id);
        reply("已开始新会话，上下文已清空。");
        return;
    }

    if (cmd == "/stop") {
        std::string session = ai_session_key(is_c2c, group, sender_id);
        bool cancelled = ai_cancel_set_if_inflight(session);
        heartbeat_finish_chat(is_c2c, group, sender_id);   // 顺手收起"思考中"气泡
        reply(cancelled ? "已停止当前回复。" : "当前没有正在生成的回复。");
        return;
    }

    if (cmd == "/compact") {
        int before = ai_context_compact(is_c2c, group, sender_id);
        int after = ai_context_count(is_c2c, group, sender_id);
        if (before <= 10) reply("当前上下文较短（" + std::to_string(before) + " 条），无需压缩。");
        else reply("已压缩会话上下文：" + std::to_string(before) + " → " + std::to_string(after) + " 条。");
        return;
    }

    if (cmd == "/retry") {
        std::string session = ai_session_key(is_c2c, group, sender_id);
        auto hist = ai_ctx_snapshot(session);
        std::string last_user, last_name, last_id;
        for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
            if (it->is_bot) continue;
            last_user = it->content; last_name = it->sender_name; last_id = it->sender_id;
            break;
        }
        if (last_user.empty()) { reply("没有可重试的消息。"); return; }
        reply("正在重新生成…");
        // 生成耗时较长 → 独立线程，避免阻塞消息接收
        bool c2c_copy = is_c2c;
        std::string group_copy = group, name_copy = last_name, id_copy = last_id,
                    content_copy = last_user;
        std::thread([this, c2c_copy, group_copy, name_copy, id_copy, content_copy]() {
            std::string s = ai_session_key(c2c_copy, group_copy, id_copy);
            ai_inflight_begin(s);
            std::string text = call_llm_api_ctx(s, content_copy, name_copy, id_copy);
            ai_inflight_end(s);
            if (ai_cancel_consume(s)) return;   // 期间被 /stop
            if (text.empty()) {
                if (c2c_copy) send_c2c_text(id_copy, "重新生成失败（模型无返回）。");
                else if (!group_copy.empty()) send_group_text(group_copy, "重新生成失败（模型无返回）。");
                return;
            }
            send_ai_reply(c2c_copy, group_copy, id_copy, text,
                          std::string(), std::string(), std::string(), id_copy);   // 支持贴纸 / @（超长自动分条）
        }).detach();
        return;
    }

    // ── 信息 / 工具类 ──
    if (cmd == "/time") {
        reply("当前时间：" + util::time_iso());
        return;
    }
    if (cmd == "/uptime") {
        uint64_t sec = (util::now_ms() - g_process_start_ms) / 1000;
        uint64_t d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60, s2 = sec % 60;
        std::string t;
        if (d) t += std::to_string(d) + " 天 ";
        if (d || h) t += std::to_string(h) + " 小时 ";
        if (d || h || m) t += std::to_string(m) + " 分 ";
        t += std::to_string(s2) + " 秒";
        reply("已运行 " + t + "\n账号: " +
              (config_.account_name.empty() ? std::string("default") : config_.account_name));
        return;
    }
    if (cmd == "/version") {
        std::string v = std::string("元宝 Bot CLI v") + kYuanbaoBotVersion;
        v += "\nC++17 · 纯命令行";
        v += "\nBot ID: " + (config_.yuanbao_id.empty() ? std::string("(未获取)") : config_.yuanbao_id);
        v += "\n已同步命令: " +
             std::to_string(sizeof(kSlashCommands) / sizeof(kSlashCommands[0]) +
                            sizeof(kPluginCommands) / sizeof(kPluginCommands[0])) + " 个";
        reply(v);
        return;
    }
    if (cmd == "/whoami") {
        std::string out = "你的用户 ID：" + sender_id + "\n";
        out += "昵称：" + sender_name + "\n";
        out += "会话：" + std::string(is_c2c ? "私聊" : ("群 " + group)) + "\n";
        out += "Bot ID：" + (config_.yuanbao_id.empty() ? std::string("(未获取)") : config_.yuanbao_id);
        reply(out);
        return;
    }
    if (cmd == "/ping") {
        reply("pong 🏓");
        return;
    }
    if (cmd == "/calc") {
        if (arg.empty()) { reply("用法：/calc (1+2)*3"); return; }
        CalcParser p(arg);
        double v = p.expr();
        p.skip();
        if (!p.ok || p.i != arg.size()) {
            reply("无法计算：" + arg + "（仅支持 + - * / 与括号）");
            return;
        }
        std::ostringstream o;
        if (v == (double)(long long)v) o << (long long)v;
        else o << v;
        reply(arg + " = " + o.str());
        return;
    }
    if (cmd == "/echo") {
        if (arg.empty()) { reply("用法：/echo 文本"); return; }
        reply(arg);
        return;
    }
    if (cmd == "/commands") {
        size_t total_cmds = sizeof(kSlashCommands) / sizeof(kSlashCommands[0]) +
                            sizeof(kPluginCommands) / sizeof(kPluginCommands[0]);
        std::string out = "可用命令（" + std::to_string(total_cmds) + " 个）：\n";
        for (auto& d : kSlashCommands) {
            out += d.name;
            out += "  ";
            out += d.desc;
            out += "\n";
        }
        for (auto& d : kPluginCommands) {
            out += d.name;
            out += "  ";
            out += d.desc;
            out += "\n";
        }
        if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(截断)";
        reply(out);
        return;
    }

    // ── MCP / Skill / 子代理 ──
    if (cmd == "/mcp") {
        if (!config_.mcp_enabled || config_.mcp_servers.empty()) {
            reply("（未配置 MCP server：在 config.json 的 MCP_SERVERS 里添加）");
            return;
        }
        std::string out = "MCP servers（" + std::to_string(config_.mcp_servers.size()) + " 个）：\n";
        for (auto& s : config_.mcp_servers) {
            out += "  " + s.name + "  " + (s.ready ? "就绪" : "未就绪") +
                   "  工具 " + std::to_string(s.tools.size()) + " 个\n";
            for (auto& t : s.tools) out += "      - " + t.name + "\n";
        }
        reply(out);
        return;
    }
    if (cmd == "/skills") {
        if (skills_.empty()) {
            reply("（未发现技能：把 OpenClaw 兼容的 skills/<名称>/SKILL.md 放到工作区目录）");
            return;
        }
        std::string out = "已加载技能 " + std::to_string(skills_.size()) + " 个：\n";
        for (auto& s : skills_) out += "  " + s.name + " - " + s.description + "\n";
        reply(out);
        return;
    }
    if (cmd == "/subagents") {
        reply("子代理：运行中 " + std::to_string(subagent_active_.load()) +
              " / 并发上限 " + std::to_string(config_.subagent_max_concurrent));
        return;
    }
    if (cmd == "/search") {
        if (arg.empty()) { reply("用法：/search 关键词"); return; }
        reply("🔎 正在搜索：" + arg);
        std::string q = arg;
        bool c2c = is_c2c;
        std::string g = group, p = sender_id;
        std::thread([this, q, c2c, g, p]() {
            YuanbaoServer::PendingTaskGuard _pg(pending_tasks_ref());
            std::string r = web_search(q, 0);
            if (r.size() > 3500) r = r.substr(0, 3500) + "\n...(截断)";
            if (c2c) send_c2c_text(p, r);
            else if (!g.empty()) send_group_text(g, r);
        }).detach();
        return;
    }
    if (cmd == "/fetch") {
        if (arg.empty()) { reply("用法：/fetch <URL>"); return; }
        reply("🌐 正在抓取：" + arg);
        std::string u = arg;
        bool c2c = is_c2c;
        std::string g = group, p = sender_id;
        std::thread([this, u, c2c, g, p]() {
            YuanbaoServer::PendingTaskGuard _pg(pending_tasks_ref());
            std::string r = web_fetch_page(u);
            if (r.size() > 3500) r = r.substr(0, 3500) + "\n...(截断)";
            if (c2c) send_c2c_text(p, r);
            else if (!g.empty()) send_group_text(g, r);
        }).detach();
        return;
    }

    // ── 运维（对齐官方插件：/restart、/yuanbao-upgrade、/issue-log）──
    if (cmd == "/restart") {
        reply("🔄 正在重启：重新加载配置并重连...");
        std::cout << "[RESTART] 收到 /restart（来自 "
                  << (is_c2c ? ("私聊 " + sender_id) : ("群 " + group)) << "）\n";
        std::string path = config_path_;
        std::string account = config_.account_name;
        // 必须放到独立线程：在当前接收线程里 join 自己会死锁
        std::thread([this, path, account]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));   // 先让回复发出
            bot_disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            bool cfg_ok = load_config(path, account, true);
            bot_connect();
            bool ready = bot_wait_ready(20000);
            std::cout << "[RESTART] 配置重载=" << (cfg_ok ? "OK" : "FAIL")
                      << " 重连=" << (ready ? "OK" : "FAIL") << "\n";
        }).detach();
        return;
    }
    if (cmd == "/yuanbao-upgrade" || cmd == "/yuanbaobot-upgrade") {
        // 对齐官方实现：真正执行升级（下载最新源码 → 编译 → 替换自身 → 重启），
        // 不再只是返回一段说明。UPGRADE_SOURCE_URL 为空时用本地源码重新编译。
        bool c2c = is_c2c;
        std::string grp = group, peer = sender_id;
        std::string url = config_.upgrade_source_url;
        std::string acct = config_.account_name;
        bool has_url = !url.empty();
        reply(std::string("⏳ 开始升级（") + (has_url ? "下载最新源码 + 编译" : "本地源码重新编译") +
              "），请稍候…\n当前版本：v" + kYuanbaoBotVersion);
        std::thread([this, c2c, grp, peer, url, has_url, acct]() {
            auto send_to = [&](const std::string& t) {
                if (c2c) send_c2c_text(peer, t);
                else if (!grp.empty()) send_group_text(grp, t);
            };
            // 1) 可选：下载最新源码（校验大小与特征，避免把错误页当源码）
            std::string src = "main.cpp";
            if (has_url && !is_safe_url(url)) {
                send_to("❌ 升级失败：UPGRADE_SOURCE_URL 不是合法的 http(s) URL");
                return;
            }
            if (has_url) {
                std::string dl = "curl -sL -k --connect-timeout 10 -m 120 \"" + url +
                                 "\" -o main.cpp.new";
                FILE* fp = popen(dl.c_str(), "r");
                if (fp) pclose(fp);
                std::string probe;
                bool ok = util::read_file_utf8("main.cpp.new", probe) &&
                          probe.size() > 50000 && probe.find("int main(") != std::string::npos;
                if (!ok) {
                    std::remove("main.cpp.new");
                    send_to("❌ 升级失败：下载的源码无效（校验未通过），已保留当前版本");
                    return;
                }
                std::cout << "[UPGRADE] 已下载新源码 " << probe.size() << " 字节\n";
                src = "main.cpp.new";
            }
            // 2) 编译到新文件名（运行中的 exe 无法覆盖）
            std::string out_exe = "main_new.exe";
            std::remove(out_exe.c_str());
#ifdef YUANBAO_PLATFORM_WINDOWS
            // 必须把 TMP/TEMP 指到 ASCII 路径：中文用户名下 g++ 汇编器无法在系统临时目录建文件
            _mkdir("C:\\yb_upgrade_tmp");
            _putenv("TMP=C:\\yb_upgrade_tmp");
            _putenv("TEMP=C:\\yb_upgrade_tmp");
            std::string compile = "g++ -std=c++17 -O2 \"" + src + "\" -o \"" + out_exe +
                                  "\" -lws2_32 -lssl -lcrypto > upgrade_build.log 2>&1";
#else
            std::string compile = "g++ -std=c++17 -O2 \"" + src + "\" -o \"" + out_exe +
                                  "\" -lpthread -lrt -lm -lssl -lcrypto > upgrade_build.log 2>&1";
#endif
            std::cout << "[UPGRADE] 编译中…\n";
            FILE* fp = popen(compile.c_str(), "r");
            if (fp) pclose(fp);
            std::string clog;
            util::read_file_utf8("upgrade_build.log", clog);
            if (clog.empty()) std::cout << "[UPGRADE] 编译命令无输出\n";
            else std::cout << "[UPGRADE] 编译输出: " << clog.substr(0, 500) << "\n";
            std::string probe2;
            if (!util::read_file_utf8(out_exe, probe2) || probe2.size() < 50000) {
                std::cout << "[UPGRADE] 编译失败:\n" << clog.substr(0, 2000) << "\n";
                std::remove(out_exe.c_str());
                send_to("❌ 升级失败：编译未通过（已保留当前版本）\n" + clog.substr(0, 600));
                return;
            }
            std::cout << "[UPGRADE] 编译成功 " << probe2.size() << " 字节\n";
            // 3) 生成替换 + 重启脚本，随后本进程退出由脚本接管
#ifdef YUANBAO_PLATFORM_WINDOWS
            {
                std::ofstream f("upgrade.bat");
                f << "@echo off\r\n"
                     "timeout /t 3 /nobreak >nul\r\n"
                     "move /Y \"" << out_exe << "\" \"main.exe\" >nul\r\n";
                if (has_url) f << "move /Y \"main.cpp.new\" \"main.cpp\" >nul\r\n";
                f << "start \"\" main.exe listen";
                if (!acct.empty()) f << " --account " << acct;
                f << "\r\ndel \"%~f0\"\r\n";
                f.close();
            }
            send_to("✅ 新版本已编译完成，正在替换并重启…\n（约 5 秒后恢复在线）");
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            // 用独立窗口启动，避免父进程退出时连带终止脚本（/b 共享控制台会被一起关掉）
            std::system("start \"\" /min upgrade.bat");
#else
            {
                std::ofstream f("upgrade.sh");
                f << "#!/bin/sh\nsleep 2\nmv -f '" << out_exe << "' 'main'\n";
                if (has_url) f << "mv -f 'main.cpp.new' 'main.cpp'\n";
                f << "./main listen";
                if (!acct.empty()) f << " --account " << acct;
                f << " >/dev/null 2>&1 &\nrm -f \"$0\"\n";
                f.close();
                chmod("upgrade.sh", 0755);
            }
            send_to("✅ 新版本已编译完成，正在替换并重启…");
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            std::system("nohup sh upgrade.sh >/dev/null 2>&1 &");
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::cout << "[UPGRADE] 退出以完成替换与重启\n";
            exit(0);
        }).detach();
        return;
    }
    if (cmd == "/issue-log") {
        if (!is_c2c) {
            reply("群聊暂不支持导出日志，请私聊发送 /issue-log");
            return;
        }
        if (!config_.owner_id.empty() && sender_id != config_.owner_id) {
            reply("⚠️ 仅 Bot 创建者可导出日志");
            return;
        }
        reply("⏳ 正在打包日志（不含 config.json 与密钥）...");
        std::string peer = sender_id;
        std::thread([this, peer]() {
            YuanbaoServer::PendingTaskGuard _pg(pending_tasks_ref());
            // 收集消息日志（不含 config.json / 密钥）
            std::string files;
#ifdef YUANBAO_PLATFORM_WINDOWS
            WIN32_FIND_DATAW ffd;
            HANDLE h = FindFirstFileW(L"./logs/messages_*.log", &ffd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    files += " \"logs/" + util::wide_to_utf8(std::wstring(ffd.cFileName)) + "\"";
                } while (FindNextFileW(h, &ffd));
                FindClose(h);
            }
#else
            DIR* d = opendir("./logs");
            if (d) {
                struct dirent* ent;
                while ((ent = readdir(d))) {
                    std::string fn = ent->d_name;
                    if (fn.rfind("messages_", 0) == 0) files += " \"logs/" + fn + "\"";
                }
                closedir(d);
            }
#endif
            std::string probe;
            if (util::read_file_utf8("logs/tasks.json", probe)) files += " \"logs/tasks.json\"";
            if (util::read_file_utf8("logs/known_groups.json", probe))
                files += " \"logs/known_groups.json\"";
            if (files.empty()) {
                send_c2c_text(peer, "没有可导出的日志");
                return;
            }
            std::string out_path = "logs/issue-log.tar.gz";
            std::string cmdline = "tar -czf \"" + out_path + "\" -C ." + files;
            FILE* fp = popen(cmdline.c_str(), "r");
            if (!fp) { send_c2c_text(peer, "打包失败：无法启动 tar"); return; }
            pclose(fp);
            std::string data;
            if (!util::read_file_utf8(out_path, data) || data.empty()) {
                send_c2c_text(peer, "打包失败（系统 tar 不可用？）");
                return;
            }
            std::string url, uuid;
            if (!upload_media("issue-log.tar.gz", data, url, uuid)) {
                send_c2c_text(peer, "日志包上传失败（检查网络 / COS）");
                return;
            }
            std::cout << "[LOG] issue-log 打包完成 " << data.size() << " 字节\n";
            send_c2c_text(peer, "日志包已生成（不含 config.json / 密钥）：\n" + url +
                                     "\n大小：" + std::to_string(data.size()) + " 字节");
        }).detach();
        return;
    }

    // ── 定时任务（/remind 一次性、/cron 周期）──
    if (cmd == "/remind" || cmd == "/cron") {
        auto trim_a = [](std::string s) {
            size_t b = s.find_first_not_of(" \t");
            size_t e = s.find_last_not_of(" \t");
            return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
        };
        std::string a = trim_a(arg);
        if (a.empty() || a == "help") {
            reply("用法：\n"
                  "/remind 10m 喝水        一次性提醒（10s/5m/2h/1d 或 18:30）\n"
                  "/cron 1h 起身活动       周期任务\n"
                  "/cron list              查看任务\n"
                  "/cron del <ID>          删除任务");
            return;
        }
        if (a == "list") {
            std::lock_guard<std::mutex> l(task_mu_);
            if (tasks_.empty()) { reply("（暂无定时任务）"); return; }
            std::string out = "定时任务（" + std::to_string(tasks_.size()) + " 个）：\n";
            for (auto& kv : tasks_) {
                auto& t = kv.second;
                out += t.id + "  " + (t.kind == "cron" ? "周期" : "提醒") + "  ";
                out += (t.is_c2c ? ("私聊" + t.peer) : ("群" + t.group)) + "  ";
                out += "下次 " + fmt_ms_time(t.next_at);
                if (t.interval_ms > 0)
                    out += "  每隔 " + std::to_string(t.interval_ms / 60000) + " 分";
                out += "\n    内容：" + t.content.substr(0, 60) + "\n";
            }
            if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(截断)";
            reply(out);
            return;
        }
        if (a.rfind("del ", 0) == 0) {
            std::string id = trim_a(a.substr(4));
            bool found = false;
            {
                std::lock_guard<std::mutex> l(task_mu_);
                auto it = tasks_.find(id);
                if (it != tasks_.end()) { tasks_.erase(it); task_save_locked(); found = true; }
            }
            reply(found ? ("已删除任务 " + id) : ("未找到任务 " + id));
            return;
        }
        // 解析「时间 内容」
        size_t sp = a.find_first_of(" \t");
        if (sp == std::string::npos) { reply("格式：/remind <时间> <内容>"); return; }
        std::string when = a.substr(0, sp);
        std::string text = trim_a(a.substr(sp + 1));
        if (text.empty()) { reply("内容不能为空"); return; }
        int64_t next_at = 0, interval = 0;
        if (!parse_when(when, next_at, interval)) {
            reply("时间格式不支持：" + when + "（用 10s/5m/2h/1d 或 18:30）");
            return;
        }
        if (cmd == "/cron") {
            interval = next_at - (int64_t)util::now_ms();
            if (interval < 60000) interval = 60000;   // 周期任务最小 1 分钟
        }
        ScheduledTask t;
        t.id = util::random_hex_id().substr(0, 8);
        t.kind = (cmd == "/cron") ? "cron" : "remind";
        t.is_c2c = is_c2c;
        t.group = group;
        t.peer = sender_id;
        t.content = text;
        t.next_at = next_at;
        t.interval_ms = interval;
        {
            std::lock_guard<std::mutex> l(task_mu_);
            tasks_[t.id] = t;
            task_save_locked();
        }
        task_start();
        std::string msg = (cmd == "/cron" ? "已创建周期任务 " : "已创建提醒 ") + t.id;
        msg += "\n下次触发：" + fmt_ms_time(t.next_at);
        if (interval > 0) msg += "\n间隔：" + std::to_string(interval / 60000) + " 分钟";
        msg += "\n内容：" + text;
        reply(msg);
        return;
    }

    // ── 媒体（贴纸 / 图片 / 文件 / 引用回复）──
    if (cmd == "/sticker") {
        if (arg.empty()) { reply("用法：/sticker 六六六（/stickers 查看全部）"); return; }
        std::string sid = stickers_.find_by_name(arg);
        if (sid.empty()) { reply("未找到贴纸：" + arg + "（/stickers 查看全部）"); return; }
        std::string js = sticker_json_of(sid, arg);
        bool ok = is_c2c ? send_c2c_sticker(sender_id, js)
                         : send_group_sticker(group, js, "", "", "");
        std::cout << "[CMD] /sticker " << arg << (ok ? " 已发送" : " 发送失败") << "\n";
        return;
    }
    if (cmd == "/stickers") {
        std::string out = "可用贴纸（" + std::to_string(stickers_.stickers.size()) + " 个）：\n";
        int n = 0;
        for (auto& kv : stickers_.stickers) {
            out += kv.first + "(" + kv.second + ")  ";
            if (++n % 4 == 0) out += "\n";
        }
        if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(截断)";
        reply(out);
        return;
    }
    if (cmd == "/img" || cmd == "/file") {
        if (is_c2c) { reply("（图片/文件发送目前仅支持群聊）"); return; }
        if (arg.empty()) { reply("用法：/" + cmd.substr(1) + " <URL 或本地路径>"); return; }
        // URL 会被拼进 curl 命令行 → 校验，防止命令注入（引号/空白可破坏参数）
        if ((arg.rfind("http://", 0) == 0 || arg.rfind("https://", 0) == 0) && !is_safe_url(arg)) {
            reply("❌ URL 含非法字符（引号/空白/控制字符），已拒绝");
            return;
        }
        reply("⏳ 正在上传并发送...");
        bool is_img = (cmd == "/img");
        std::string src = arg, grp = group;
        std::thread([this, is_img, src, grp]() {
            YuanbaoServer::PendingTaskGuard _pg(pending_tasks_ref());
            std::string data, base = src;
            if (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0) {
                std::ostringstream c;
                c << "curl -s -k -L --connect-timeout 10 -m 60 \"" << src << "\"";
                FILE* fp = popen(c.str().c_str(), "rb");
                if (fp) {
                    char buf[65536];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) data.append(buf, n);
                    pclose(fp);
                }
                size_t q = base.find('?');
                if (q != std::string::npos) base = base.substr(0, q);
                size_t sl = base.find_last_of('/');
                if (sl != std::string::npos) base = base.substr(sl + 1);
            } else {
                util::read_file_utf8(src, data);
                size_t sl = base.find_last_of("/\\");
                if (sl != std::string::npos) base = base.substr(sl + 1);
            }
            if (data.empty() || base.empty()) {
                send_group_text(grp, "发送失败：文件为空或无法读取");
                return;
            }
            if (data.size() > 20 * 1024 * 1024) {
                send_group_text(grp, "发送失败：文件超过 20MB 限制");
                return;
            }
            std::string url, uuid;
            if (!upload_media(base, data, url, uuid)) {
                send_group_text(grp, "发送失败：上传 COS 失败");
                return;
            }
            bool ok = false;
            if (is_img) {
                int w = 0, h = 0;
                util::parse_image_dimensions(data, w, h);
                ok = send_group_image(grp, url, uuid, (int)data.size(), w, h);
            } else {
                ok = send_group_file(grp, url, base, uuid, (int)data.size());
            }
            std::cout << "[CMD] " << (is_img ? "img" : "file") << " " << base
                      << (ok ? " 已发送" : " 发送失败") << "\n";
            send_group_text(grp, ok ? ("已发送：" + base) : ("发送失败：" + base));
        }).detach();
        return;
    }
    if (cmd == "/reply") {
        // 用法：/reply [消息ID] <文本>；只给文本时引用最近一条消息
        std::string ref, text = arg;
        size_t sp = arg.find_first_of(" \t");
        if (sp != std::string::npos) {
            std::string first = arg.substr(0, sp);
            if (first.size() > 12 && first.find('-') != std::string::npos) {
                ref = first;
                text = arg.substr(sp + 1);
            }
        }
        if (ref.empty()) {
            std::lock_guard<std::mutex> l(msg_mu_);
            if (!msg_cache_.empty()) ref = msg_cache_.back()["msg_id"].asString();
        }
        if (text.empty()) { reply("用法：/reply [消息ID] <文本>"); return; }
        if (is_c2c) {
            send_c2c_text(sender_id, text);   // 私聊不支持引用，退化为普通消息
            return;
        }
        if (ref.empty()) { reply("（没有可引用的消息，可直接发文本）"); return; }
        bool ok = send_group_reply(group, text, ref);
        std::cout << "[CMD] /reply ref=" << ref.substr(0, 24)
                  << (ok ? " 已发送" : " 发送失败") << "\n";
        return;
    }

    // ── 群 / 历史 ──
    if (cmd == "/groups") {
        if (known_groups_.empty()) {
            reply("（暂无已知群：先让机器人收到群消息，或在 config.json 配置 LISTEN_GROUPS）");
            return;
        }
        std::string out = "已知群 " + std::to_string(known_groups_.size()) + " 个：\n";
        for (auto& kv : known_groups_)
            out += "  " + kv.first + (kv.second.empty() ? "" : "  " + kv.second) + "\n";
        if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(截断)";
        reply(out);
        return;
    }
    if (cmd == "/groupinfo" || cmd == "/members") {
        std::string g = arg;
        if (g.empty() && !is_c2c) g = group;
        if (g.empty()) { reply("用法：/" + cmd.substr(1) + " <群号>"); return; }
        reply("⏳ 正在查询群 " + g + " ...");
        // 查询由接收线程回包，放独立线程避免阻塞
        bool c2c = is_c2c;
        std::string grp = g, peer = sender_id, sub = cmd;
        std::thread([this, c2c, grp, peer, sub]() {
            YuanbaoServer::PendingTaskGuard _pg(pending_tasks_ref());
            std::vector<std::pair<std::string, std::string>> members;
            std::string owner, gname;
            bool ok = fetch_group_members_sync(grp, members, owner, gname, 15000);
            std::string out;
            if (!ok) {
                out = "查询失败或超时（群号 " + grp + "）";
            } else if (sub == "/groupinfo") {
                out = "群：" + grp + (gname.empty() ? "" : "（" + gname + "）");
                out += "\n成员数：" + std::to_string(members.size());
                if (!owner.empty()) out += "\n群主：" + owner;
            } else {
                out = "群 " + grp + " 成员（" + std::to_string(members.size()) + " 人）：\n";
                int n = 0;
                for (auto& m : members) {
                    if (++n > 100) { out += "...(更多省略)\n"; break; }
                    out += "  " + m.first + (m.second.empty() ? "" : "  " + m.second) + "\n";
                }
            }
            if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(截断)";
            if (c2c) send_c2c_text(peer, out);
            else if (!grp.empty()) send_group_text(grp, out);
        }).detach();
        return;
    }
    if (cmd == "/history") {
        int n = std::atoi(arg.c_str());
        if (n <= 0) n = 10;
        if (n > 50) n = 50;
        std::string latest;
#ifdef YUANBAO_PLATFORM_WINDOWS
        WIN32_FIND_DATAW ffd;
        HANDLE h = FindFirstFileW(L"./logs/messages_*.log", &ffd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::string fn = util::wide_to_utf8(std::wstring(ffd.cFileName));
                if (fn > latest) latest = fn;
            } while (FindNextFileW(h, &ffd));
            FindClose(h);
        }
#else
        DIR* d = opendir("./logs");
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d))) {
                std::string fn = ent->d_name;
                if (fn.rfind("messages_", 0) == 0 && fn.size() > 4 &&
                    fn.compare(fn.size() - 4, 4, ".log") == 0 && fn > latest) latest = fn;
            }
            closedir(d);
        }
#endif
        if (latest.empty()) { reply("（暂无消息日志）"); return; }
        std::ifstream f("./logs/" + latest);
        if (!f.is_open()) { reply("（无法读取日志）"); return; }
        std::deque<std::string> lines;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            JsonVal v;
            if (!json_parse(line, v)) continue;
            if (!is_c2c && v["group_code"].asString() != group) continue;
            lines.push_back(line);
            if ((int)lines.size() > n) lines.pop_front();
        }
        if (lines.empty()) { reply("（该会话暂无历史消息）"); return; }
        std::string out;
        for (auto& l : lines) {
            JsonVal v;
            if (!json_parse(l, v)) continue;
            out += "[" + v["timestamp"].asString() + "] ";
            out += (v["sender_name"].asString().empty() ? v["sender_id"].asString()
                                                       : v["sender_name"].asString());
            out += ": " + v["content"].asString() + "\n";
        }
        if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(截断)";
        reply(out);
        return;
    }
    if (cmd == "/inspect") {
        int idx = std::atoi(arg.c_str());
        std::lock_guard<std::mutex> l(msg_mu_);
        if (msg_cache_.empty()) { reply("（暂无消息缓存）"); return; }
        if (idx <= 0) idx = 1;
        int pos = (int)msg_cache_.size() - idx;   // 1 = 最新
        if (pos < 0 || pos >= (int)msg_cache_.size()) {
            reply("序号超出范围（共 " + std::to_string(msg_cache_.size()) + " 条，1=最新）");
            return;
        }
        JsonVal m = msg_cache_[pos];
        std::string out = "消息详情：\n";
        out += "msg_id: " + m["msg_id"].asString() + "\n";
        out += "会话: " + std::string(m["is_c2c"].asBool()
                    ? ("私聊 " + m["c2c_peer"].asString())
                    : ("群 " + m["group_code"].asString())) + "\n";
        out += "发送者: " + m["sender_name"].asString() + " (" + m["sender_id"].asString() + ")\n";
        out += "时间: " + m["timestamp"].asString() + "\n";
        out += "内容: " + m["content"].asString();
        if (out.size() > 3500) out = out.substr(0, 3500) + "\n...(截断)";
        reply(out);
        return;
    }

    // ── 命令执行（⚠️ 高风险，需 EXEC_ENABLED）──
    if (cmd == "/exec") {
        if (!config_.exec_enabled) {
            reply("（命令执行未开启：config.json 设 EXEC_ENABLED=true 后重启）");
            return;
        }
        std::string deny;
        if (!exec_allowed(is_c2c, sender_id, deny)) { reply("（命令执行不可用：" + deny + "）"); return; }
        if (arg.empty()) { reply("用法：/exec <命令>（例如 /exec dir）"); return; }
        if (config_.exec_deny_enabled && exec_is_denied(arg)) {
            std::cerr << "[EXEC] 黑名单拦截: " << arg << "\n";
            reply("⚠️ 该命令命中安全黑名单，已拒绝执行。");
            return;
        }
        reply("⏳ 正在执行：\n" + arg);
        run_exec_async(is_c2c, group, sender_id, arg);
        return;
    }

    // ── 文件操作（需 FILE_OPS_ENABLED / EXEC_* 权限范围）──
    if (cmd == "/ls" || cmd == "/read" || cmd == "/write" || cmd == "/append") {
        if (!config_.file_ops_enabled) {
            reply("（文件功能未开启：config.json 设 FILE_OPS_ENABLED=true 后重启）");
            return;
        }
        std::string deny;
        if (!exec_allowed(is_c2c, sender_id, deny)) { reply("（文件功能不可用：" + deny + "）"); return; }

        if (cmd == "/ls") {
            bool ok = false;
            std::string body = dir_list_text(arg, ok);
            std::cout << "[FILE] ls " << (arg.empty() ? "." : arg) << (ok ? " OK" : " 失败") << "\n";
            reply((ok ? "目录内容：" : "") + std::string("\n") + body);
            return;
        }
        if (cmd == "/read") {
            if (arg.empty()) { reply("用法：/read <文件路径>"); return; }
            bool ok = false;
            std::string body = file_read_text(arg, ok);
            std::cout << "[FILE] read " << arg << (ok ? " OK" : " 失败")
                      << "，内容: " << body.substr(0, 120) << "\n";
            reply((ok ? "文件内容：" : "") + std::string("\n") + body);
            return;
        }
        // /write 与 /append：格式「路径|内容」
        size_t bar = arg.find_first_of("|｜");
        if (arg.empty() || bar == std::string::npos) {
            reply("用法：/" + cmd.substr(1) + " <文件路径>|<内容>（用竖线分隔路径与内容）");
            return;
        }
        std::string path = arg.substr(0, bar);
        std::string content = arg.substr(bar + 1);
        while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(0, 1);
        while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
        if (path.empty()) { reply("（路径为空）"); return; }
        std::string err;
        bool ok = file_write_text(path, content, cmd == "/append", err);
        if (ok) {
            std::cout << "[FILE] " << (cmd == "/append" ? "追加" : "写入") << " " << path
                      << " (" << content.size() << " 字节)\n";
            reply(std::string("已") + (cmd == "/append" ? "追加" : "写入") + " " +
                  std::to_string(content.size()) + " 字节 → " + path);
        } else {
            reply("写入失败：" + err);
        }
        return;
    }

    reply("未知命令：" + cmd + "（发送 /help 查看可用命令）");
}

// =====================================================================
//  大模型 API 调用
// =====================================================================
// 通用多轮对话请求（messages：role/content 列表，按时间顺序）
std::string YuanbaoServer::llm_chat(const std::vector<std::pair<std::string, std::string>>& messages) {
    if (config_.llm.api_url.empty() || config_.llm.api_key.empty()) {
        std::cerr << "[LLM] API 未配置\n";
        return "";
    }

    auto esc = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    };

    std::string sys_prompt = config_.llm.system_prompt;
    if (sys_prompt.empty())
        sys_prompt = "你是一个友好的群聊机器人助手。请用简洁、有趣、口语化的中文回复，控制在 2-3 句话以内。";
    // 告知模型可用的富文本能力（发表情贴纸 / @群成员），否则不会输出 [sticker:...] 指令
    sys_prompt += ai_capability_prompt();

    // 构建请求体 (OpenAI 兼容格式)：system + 多轮历史 + 当前消息
    std::ostringstream body;
    body << "{\"model\":\"" << esc(config_.llm.model) << "\",\"messages\":["
         << "{\"role\":\"system\",\"content\":\"" << esc(sys_prompt) << "\"}";
    for (auto& m : messages) {
        body << ",{\"role\":\"" << esc(m.first) << "\",\"content\":\"" << esc(m.second) << "\"}";
    }
    body << "],\"max_tokens\":" << config_.llm.max_tokens
         << ",\"temperature\":" << config_.llm.temperature
         << "}";

    std::string body_str = body.str();
    std::cout << "[LLM] 请求模型: " << config_.llm.model
              << " 上下文消息数=" << messages.size()
              << " max_tokens=" << config_.llm.max_tokens
              << " temp=" << config_.llm.temperature << "\n";

    // 发送 HTTPS 请求
    std::string resp_body;

#ifdef YUANBAO_NO_OPENSSL
    // 无 OpenSSL 时使用 curl 命令行
    {
        std::ostringstream cmd;
        cmd << "curl -s -k -X POST \"" << config_.llm.api_url << "\""
            << " -H \"Content-Type: application/json\""
            << " -H \"Authorization: Bearer " << config_.llm.api_key << "\""
            << " --max-time " << config_.llm.timeout_sec
            << " -d '" << body_str << "'";
        FILE* fp = popen(cmd.str().c_str(), "r");
        if (!fp) {
            std::cerr << "[LLM] curl 执行失败\n";
            return "";
        }
        char buf[65536];
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        pclose(fp);
        resp_body = std::string(buf, n);
    }
#else
    {
        // 解析 API URL
        std::string url = config_.llm.api_url;
        std::string host, path = "/";
        int port = 443;

        if (url.find("https://") == 0) url = url.substr(8);
        else if (url.find("http://") == 0) { url = url.substr(7); port = 80; }

        size_t slash = url.find('/');
        if (slash != std::string::npos) {
            host = url.substr(0, slash);
            path = url.substr(slash);
        } else {
            host = url;
        }

        size_t colon = host.find(':');
        if (colon != std::string::npos) {
            port = safe_stoi(host.substr(colon + 1), port);
            host = host.substr(0, colon);
        }

        std::map<std::string, std::string> headers;
        headers["Authorization"] = "Bearer " + config_.llm.api_key;
        int timeout_ms = config_.llm.timeout_sec * 1000;

        int ret = https_post(host, port, path, body_str, headers, resp_body, timeout_ms);
        if (ret != 0) {
            std::cerr << "[LLM] HTTPS 请求失败 ret=" << ret << "\n";
            return "";
        }
    }
#endif

    // 解析响应
    JsonVal root;
    if (!json_parse(resp_body, root)) {
        std::cerr << "[LLM] JSON 解析失败: " << resp_body.substr(0, 200) << "\n";
        return "";
    }

    // 检查错误
    if (!root["error"]["message"].asString().empty()) {
        std::cerr << "[LLM] API 错误: " << root["error"]["message"].asString() << "\n";
        return "";
    }

    // 提取回复内容 (OpenAI 格式: choices[0].message.content)
    std::string reply, finish_reason;
    if (!root["choices"].arr.empty()) {
        reply = root["choices"][0]["message"]["content"].asString();
        finish_reason = root["choices"][0]["finish_reason"].asString();
    }
    // 兼容：content 为空但服务返回了 reasoning_content / reasoning（部分模型把内容放这里）
    if (reply.empty() && !root["choices"].arr.empty()) {
        std::string rc = root["choices"][0]["message"]["reasoning_content"].asString();
        if (rc.empty()) rc = root["choices"][0]["message"]["reasoning"].asString();
        if (!rc.empty()) {
            size_t p = rc.find_last_of('\n');   // 只取最后一段，避免把整段思考过程发出去
            reply = (p == std::string::npos) ? rc : rc.substr(p + 1);
            if (reply.empty()) reply = rc;
            std::cout << "[LLM] content 为空，改用 reasoning 末段兜底\n";
        }
    }
    if (reply.empty()) {
        std::cerr << "[LLM] 响应中未找到有效回复"
                  << (finish_reason.empty() ? "" : ("（finish_reason=" + finish_reason + "）"))
                  << "，响应片段: " << resp_body.substr(0, 300) << "\n";
        return "";
    }
    if (finish_reason == "length")
        std::cerr << "[LLM] 提示：回复被 max_tokens=" << config_.llm.max_tokens
                  << " 截断（可调大 LLM_MAX_TOKENS）\n";
    // 去除首尾空白
    while (!reply.empty() && (reply.front() == ' ' || reply.front() == '\n')) reply.erase(0, 1);
    while (!reply.empty() && (reply.back() == ' ' || reply.back() == '\n')) reply.pop_back();
    std::cout << "[LLM] 回复: " << reply.substr(0, 100) << "\n";
    return reply;
}

// 单轮调用（ai-test / 无上下文场景）
std::string YuanbaoServer::call_llm_api(const std::string& user_message, const std::string& sender_name) {
    (void)sender_name;
    std::vector<std::pair<std::string, std::string>> msgs;
    msgs.push_back({"user", user_message});
    return llm_chat(msgs);
}

// 带会话历史的多轮调用：bot 发言作为 assistant，其他人发言作为 user（带昵称前缀）
// 构建带会话历史 + 窗口预算裁剪的请求消息（call_llm_api_ctx / ai_agent_reply 共用）
std::vector<std::pair<std::string, std::string>>
YuanbaoServer::build_ctx_msgs(const std::string& session, const std::string& user_message,
                              const std::string& sender_name, const std::string& sender_id) {
    std::vector<std::pair<std::string, std::string>> msgs;
    auto history = ai_ctx_snapshot(session);
    for (auto& h : history) {
        if (h.is_bot) {
            msgs.push_back({"assistant", h.content});
        } else {
            std::string name = h.sender_name.empty() ? h.sender_id : h.sender_name;
            std::string c = h.content;
            if (c.size() > 8000) c = c.substr(0, 8000) + "...";   // 单条超长时截断
            msgs.push_back({"user", "[" + name + "]: " + c});
        }
    }
    // 历史最后一条若就是本次消息（listen 场景刚记录过），则不重复追加
    bool last_is_current = !history.empty() && !history.back().is_bot &&
                           history.back().sender_id == sender_id && history.back().content == user_message;
    if (!last_is_current) {
        std::string name = sender_name.empty() ? sender_id : sender_name;
        msgs.push_back({"user", "[" + name + "]: " + user_message});
    }

    // 上下文窗口预算裁剪：模型窗口 512K token，先扣除输出预留（max_tokens），
    // 中文按 1 字符 ≈ 1 token 保守估算，再留 10% 余量；从最近的消息往回保留
    {
        size_t window = 512000;
        size_t reserve = (size_t)(config_.llm.max_tokens > 0 ? config_.llm.max_tokens : 0);
        size_t budget = (window > reserve + 20000) ? (window - reserve) : (window / 2);
        budget = budget / 10 * 9;
        size_t total = 0;
        size_t keep_from = msgs.size();
        for (size_t i = msgs.size(); i-- > 0; ) {
            size_t len = msgs[i].second.size() + 24;   // 加上角色/昵称前缀开销
            if (total + len > budget && keep_from < msgs.size()) break;
            total += len;
            keep_from = i;
        }
        if (keep_from > 0) {
            msgs.erase(msgs.begin(), msgs.begin() + (long)keep_from);
            while (!msgs.empty() && msgs.front().first == "assistant") msgs.erase(msgs.begin());
            std::cout << "[LLM] 按 512K 窗口预算裁剪上下文，保留最近 " << msgs.size()
                      << " 条（约 " << (total / 1000) << "K 字符）\n";
        }
    }
    return msgs;
}

std::string YuanbaoServer::call_llm_api_ctx(const std::string& session, const std::string& user_message,
                                            const std::string& sender_name, const std::string& sender_id) {
    auto msgs = build_ctx_msgs(session, user_message, sender_name, sender_id);
    std::string r = llm_chat(msgs);
    if (r.empty()) {
        // 偶发空响应（超时 / 限流 / 网络抖动）→ 1 秒后重试一次
        std::cout << "[LLM] 首次未取得回复，1 秒后重试一次...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        r = llm_chat(msgs);
    }
    return r;
}

// 解析模型回复里的气泡控制指令：[bubble:on]/[bubble:off]（也接受 [气泡:...]）
// action: 1=RUNNING（显示/续期）, 2=FINISH（收起）；返回是否找到
static bool parse_bubble_cmd(const std::string& text, int& action, int& seconds) {
    action = 0;
    seconds = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t lb = text.find('[', pos);
        if (lb == std::string::npos) break;
        size_t rb = text.find(']', lb + 1);
        if (rb == std::string::npos) break;
        std::string inner = text.substr(lb + 1, rb - lb - 1);
        size_t colon = inner.find_first_of(":：");
        if (colon != std::string::npos) {
            std::string k = inner.substr(0, colon);
            std::string v = inner.substr(colon + 1);
            auto trim = [](std::string s) {
                size_t b = s.find_first_not_of(" \t\r\n");
                size_t e = s.find_last_not_of(" \t\r\n");
                return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
            };
            k = trim(k);
            v = trim(v);
            for (auto& c : k) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            for (auto& c : v) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (k == "bubble" || k == "气泡") {
                // 支持 [bubble:on|120]（保活 120 秒）与 [bubble:off|5]（5 秒后收起）
                std::string vv = v;
                size_t wp;
                while ((wp = vv.find("｜")) != std::string::npos) vv.replace(wp, 3, "|");
                int sec = 0;
                std::string state = vv;
                size_t bar = vv.find('|');
                if (bar != std::string::npos) {
                    state = vv.substr(0, bar);
                    std::string num = vv.substr(bar + 1);
                    for (auto& c : num) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                    if (num == "forever" || num == "inf" || num == "always" || num == "永久")
                        sec = 86400;
                    else
                        sec = std::atoi(num.c_str());
                    if (sec < 0) sec = 0;
                    if (sec > 86400) sec = 86400;
                }
                state = trim(state);
                if (state == "on" || state == "running" || state == "1" || state == "开" ||
                    state == "show" || state == "hold" || state == "auto" || state == "reset")
                    action = 1;
                else
                    action = 2;
                seconds = sec;
                return true;
            }
        }
        pos = rb + 1;
    }
    return false;
}

// 解析模型回复里的"工具调用"（exec / read / write / append / ls）
static std::vector<std::pair<std::string, std::string>> parse_tool_calls(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    auto trim = [](std::string s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    };
    size_t pos = 0;
    while (pos < text.size()) {
        size_t lb = text.find('[', pos);
        if (lb == std::string::npos) break;
        size_t rb = text.find(']', lb + 1);
        if (rb == std::string::npos) break;
        std::string inner = text.substr(lb + 1, rb - lb - 1);
        size_t colon = inner.find_first_of(":：");
        if (colon != std::string::npos) {
            std::string key = trim(inner.substr(0, colon));
            std::string val = trim(inner.substr(colon + 1));
            for (auto& c : key) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            std::string tool;
            if (key == "exec" || key == "sh" || key == "cmd" || key == "执行") tool = "exec";
            else if (key == "read" || key == "读") tool = "read";
            else if (key == "write" || key == "写") tool = "write";
            else if (key == "append" || key == "追加") tool = "append";
            else if (key == "ls" || key == "dir" || key == "目录") tool = "ls";
            else if (key == "mcp") tool = "mcp";
            else if (key == "skill" || key == "技能") tool = "skill";
            else if (key == "agent" || key == "subagent" || key == "子代理") tool = "agent";
            else if (key == "search" || key == "搜索" || key == "websearch") tool = "search";
            else if (key == "fetch" || key == "抓取" || key == "web") tool = "fetch";
            if (!tool.empty() && !val.empty()) out.push_back(std::make_pair(tool, val));
        }
        pos = rb + 1;
    }
    return out;
}

// 执行单个工具调用（含权限/黑名单校验），返回给模型看的文本结果
std::string YuanbaoServer::exec_tool_for_model(const std::string& tool, const std::string& arg,
                                               bool is_c2c, const std::string& group,
                                               const std::string& sender_id, bool allow_agent) {
    std::string deny;

    // ── MCP 工具 ──
    if (tool == "mcp") {
        if (!config_.mcp_enabled) return "（MCP 未开启：config.json 设 MCP_ENABLED=true）";
        if (!exec_allowed(is_c2c, sender_id, deny)) return "（无权限：" + deny + "）";
        size_t bar = arg.find('|');
        std::string st = (bar == std::string::npos) ? arg : arg.substr(0, bar);
        std::string js = (bar == std::string::npos) ? std::string("{}") : arg.substr(bar + 1);
        return mcp_call(trim_s(st), trim_s(js));
    }
    // ── Skill：按需返回正文 ──
    if (tool == "skill") {
        std::string b = skill_body(trim_s(arg));
        return b.empty() ? ("（未找到技能：" + trim_s(arg) + "）") : b;
    }
    // ── 子代理 ──
    if (tool == "agent") {
        if (!allow_agent) return "（子代理不能再派生其他子代理）";
        return ai_subagent(arg, is_c2c, group, sender_id);
    }
    // ── 联网搜索 / 网页抓取（只读外部信息，不受 EXEC 权限限制）──
    if (tool == "search") return web_search(trim_s(arg), 0);
    if (tool == "fetch") return web_fetch_page(trim_s(arg));

    if (tool == "exec") {
        if (!config_.exec_enabled || !config_.ai_exec_enabled)
            return "（未开启：需要 config.json 里 EXEC_ENABLED=true 且 AI_EXEC_ENABLED=true）";
    } else {
        if (!config_.file_ops_enabled)
            return "（未开启：需要 config.json 里 FILE_OPS_ENABLED=true）";
    }
    if (!exec_allowed(is_c2c, sender_id, deny)) return "（无权限：" + deny + "）";

    if (tool == "exec") {
        if (config_.exec_deny_enabled && exec_is_denied(arg))
            return "（该命令命中安全黑名单，已拒绝执行）";
        bool to = false;
        std::string out = run_shell_command(arg, config_.exec_timeout_sec, to);
        if (out.size() > 6000) out = out.substr(0, 6000) + "\n...(输出截断)";
        return out;
    }
    if (tool == "ls") {
        bool ok = false;
        std::string out = dir_list_text(arg, ok);
        if (out.size() > 6000) out = out.substr(0, 6000) + "\n...(截断)";
        return out;
    }
    if (tool == "read") {
        bool ok = false;
        std::string out = file_read_text(arg, ok);
        if (out.size() > 6000) out = out.substr(0, 6000) + "\n...(截断)";
        return out;
    }
    // write / append：格式「路径|内容」
    size_t bar = arg.find_first_of("|｜");
    if (bar == std::string::npos) return "（格式应为 路径|内容）";
    std::string path = arg.substr(0, bar);
    std::string content = arg.substr(bar + 1);
    while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(0, 1);
    while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
    if (path.empty()) return "（路径为空）";
    std::string err;
    bool ok = file_write_text(path, content, tool == "append", err);
    return ok ? ("已写入 " + std::to_string(content.size()) + " 字节 → " + path)
              : ("写入失败：" + err);
}

// 带工具循环的回复生成：模型输出工具指令 → 执行 → **结果回填给模型** → 继续推理，
// 直到模型产出自然语言回复（最多 5 轮；单轮最多 8 个工具调用）
std::string YuanbaoServer::ai_agent_reply(bool is_c2c, const std::string& group, const std::string& peer,
                                          const std::string& user_message, const std::string& sender_name,
                                          const std::string& sender_id) {
    std::string session = ai_session_key(is_c2c, group, peer);
    auto msgs = build_ctx_msgs(session, user_message, sender_name, sender_id);
    std::string reply;
    const int MAX_ROUNDS = 5;
    for (int round = 0; round < MAX_ROUNDS; round++) {
        reply = llm_chat(msgs);
        if (reply.empty() && round == 0) {
            std::cout << "[LLM] 首次未取得回复，1 秒后重试一次...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            reply = llm_chat(msgs);
        }
        if (reply.empty()) break;
        // 气泡控制：[bubble:on]/[bubble:off] 立即生效（不产生工具结果）
        {
            int bubble_act = 0, bubble_sec = 0;
            if (parse_bubble_cmd(reply, bubble_act, bubble_sec))
                ai_bubble_control(is_c2c, group, sender_id, bubble_act == 1, bubble_sec);
        }
        auto calls = parse_tool_calls(reply);
        if (calls.empty()) {
            // 模型偶尔会输出“执行结果：”之类的占位文字（既无指令也无实质内容）→ 提醒后重问一轮
            std::string t = reply;
            size_t b = t.find_first_not_of(" \t\r\n");
            size_t e = t.find_last_not_of(" \t\r\n");
            t = (b == std::string::npos) ? std::string() : t.substr(b, e - b + 1);
            if (!t.empty() && t.size() <= 10 && t.find("执行") != std::string::npos) {
                std::cout << "[AGENT] 模型输出占位文字，提醒后重问一轮\n";
                msgs.push_back({"assistant", reply});
                msgs.push_back({"user",
                    "（请不要输出“执行结果”这类占位文字：需要查看信息就直接输出 [exec:命令] / "
                    "[ls:目录] / [read:路径] 指令；否则请直接用自然语言回答用户。）"});
                continue;
            }
            break;   // 正常的最终回复
        }
        if ((int)calls.size() > 8) calls.resize(8);
        std::cout << "[AGENT] 第 " << (round + 1) << " 轮：模型请求 " << calls.size() << " 个工具调用\n";
        msgs.push_back({"assistant", reply});
        std::string results;
        for (auto& c : calls) {
            std::string out = exec_tool_for_model(c.first, c.second, is_c2c, group, sender_id);
            std::cout << "[AGENT] " << c.first << " " << c.second.substr(0, 120)
                      << " → " << out.substr(0, 200) << "\n";
            results += "【" + c.first + ": " + c.second + "】\n" + out + "\n\n";
        }
        msgs.push_back({"user",
            "（以上是工具执行结果。请基于结果用自然语言回答用户的问题："
            "不要输出原始日志或代码块，不要复述本提示，不要输出 [exec:...] 之类的指令。）\n" + results});
    }
    return reply;
}

// 对外入口：记录本次输入（若尚未记录）→ 携带会话历史生成回复
std::string YuanbaoServer::ai_generate_ctx(bool is_c2c, const std::string& group, const std::string& peer,
                                           const std::string& message, const std::string& sender_name,
                                           const std::string& sender_id) {
    std::string session = ai_session_key(is_c2c, group, peer);
    auto history = ai_ctx_snapshot(session);
    bool already = !history.empty() && !history.back().is_bot &&
                   history.back().sender_id == sender_id && history.back().content == message;
    if (!already) {
        ai_context_append(false, is_c2c, group, peer, sender_id, sender_name, message);
    }
    // 走工具循环：模型请求的 exec/read/write/ls 会在循环内执行并把结果回填给模型
    return ai_agent_reply(is_c2c, group, peer, message, sender_name, sender_id);
}

bool YuanbaoServer::bot_connect() {
#ifdef YUANBAO_NO_OPENSSL
    std::cerr << "[Bot] ========================================\n";
    std::cerr << "[Bot] YUANBAO_NO_OPENSSL 模式: WSS 不可用\n";
    std::cerr << "[Bot] Bot 连接功能需要 OpenSSL 支持\n";
    std::cerr << "[Bot] 请安装 OpenSSL 后重新编译:\n";
    std::cerr << "[Bot]   Linux:   sudo apt install libssl-dev\n";
    std::cerr << "[Bot]   macOS:   brew install openssl\n";
    std::cerr << "[Bot]   MSYS2:   pacman -S mingw-w64-x86_64-openssl\n";
    std::cerr << "[Bot]   MinGW:   下载 winlibs 带 OpenSSL 版本\n";
    std::cerr << "[Bot] 前端控制台和 HTTP API 仍然可用\n";
    std::cerr << "[Bot] ========================================\n";
    return false;
#else
    // ← 修复：若连接线程已在运行且未主动断开，直接复用，避免重复调用 /api/connect
    //   （页面刷新/重复点连接）时杀旧线程 → 重建 → 再杀的循环，该循环造成 Bot
    //   反复断线重连（SSL 错误 5）与高频查询，是卡顿的重要来源
    if (bot_running_.load()) return true;
    bot_running_ = true;
    sync_sent_ = false;
    bot_ws_thread_ = std::thread([this]() {
        try {
        int retry = 0;
        while (bot_running_.load()) {
            sync_sent_ = false;  // 每次重试重置
            if (!bot_sign_token()) {
                std::cerr << "[Bot] 鉴权失败，等待重试...\n";
                std::this_thread::sleep_for(std::chrono::seconds(5));
                retry++;
                continue;
            }
            if (!bot_ws_connect()) {
                std::cerr << "[Bot] WebSocket 连接失败，等待重试...\n";
                std::this_thread::sleep_for(std::chrono::seconds(5));
                retry++;
                continue;
            }
            
            // 发送认证消息
            auto auth_frame = bot_ws_build_auth();
            std::cerr << "[Bot] 发送 Auth 帧, 长度=" << auth_frame.size() << "\n";
            std::cerr << "[Bot] Auth 帧 HEX(前100): ";
            for (size_t i = 0; i < std::min((size_t)100, auth_frame.size()); i++)
                fprintf(stderr, "%02x", auth_frame[i]);
            fprintf(stderr, "\n");
            if (!bot_ws_send_bytes(auth_frame)) {
                std::cerr << "[Bot] Auth 消息发送失败!\n";
                bot_connected_ = false;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                retry++;
                continue;
            }
            std::cerr << "[Bot] Auth 消息已发送\n";
            
            // 标记已连接，进入接收循环（先不加 sync，测试连接稳定性）
            bot_connected_ = true;
            std::cerr << "[Bot] 进入消息接收循环...\n";
            bot_ws_recv_loop();
            
            // 接收循环退出 = 连接断开，尝试重连
            std::cerr << "[Bot] 连接断开，尝试重连...\n";
            bot_connected_ = false;
#ifndef YUANBAO_NO_OPENSSL
            if (bot_ws_ssl_) { SSL_shutdown(bot_ws_ssl_); SSL_free(bot_ws_ssl_); bot_ws_ssl_ = nullptr; }
            if (bot_ws_ssl_ctx_) { SSL_CTX_free(bot_ws_ssl_ctx_); bot_ws_ssl_ctx_ = nullptr; }
#endif
            if (bot_ws_fd_ >= 0) { close_socket(bot_ws_fd_); bot_ws_fd_ = -1; }
            retry++;
            // 指数退避重试（上限 60s），永不放弃，保证 Bot 断线后自动恢复
            int delay = (retry <= 5) ? (1 << (retry - 1)) : (retry > 20 ? 60 : 16);  // 1,2,4,8,16,16...60
            std::cerr << "[Bot] " << delay << "s 后重试 (第" << retry << "次)...\n";
            std::this_thread::sleep_for(std::chrono::seconds(delay));
            continue;
        }
        std::cerr << "[Bot] connect 线程退出\n";
        } catch (const std::exception& e) {
            std::cerr << "[Bot] connect 线程异常: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[Bot] connect 线程未知异常\n";
        }
    });
    return true;
#endif
}

void YuanbaoServer::bot_disconnect() {
    bot_running_ = false;
    // 先关闭 socket，让 SSL_read/recv 立即返回错误
    if (bot_ws_fd_ >= 0) {
        close_socket(bot_ws_fd_);
        bot_ws_fd_ = -1;
    }
#ifndef YUANBAO_NO_OPENSSL
    if (bot_ws_ssl_) { SSL_free(bot_ws_ssl_); bot_ws_ssl_ = nullptr; }
    if (bot_ws_ssl_ctx_) { SSL_CTX_free(bot_ws_ssl_ctx_); bot_ws_ssl_ctx_ = nullptr; }
#endif
    // 等待线程退出（socket 已关闭，recv loop 会立即退出）
    if (bot_ws_thread_.joinable()) {
        auto start = std::chrono::steady_clock::now();
        while (bot_ws_thread_.joinable()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(3)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (bot_ws_thread_.joinable()) bot_ws_thread_.detach();
    }
    bot_connected_ = false;
    client_token_.clear();
}

// =====================================================================
//  保存配置
// =====================================================================
void YuanbaoServer::save_config() {
    // 安全回写：只更新当前账号的 YUANBAO_ID，保留 ACCOUNTS 结构与文件其它内容。
    // （旧实现会整体重写为旧格式，破坏多账号配置）
    if (config_.yuanbao_id.empty()) return;
    const std::string path = config_path_.empty() ? "config.json" : config_path_;

    std::string content;
    if (!util::read_file_utf8(path, content)) return;

    // 定位当前账号对象文本范围（ACCOUNTS 内按 NAME 匹配）
    size_t begin = 0, end = content.size();
    bool found_block = false;
    if (!config_.account_name.empty()) {
        size_t pos = 0;
        while ((pos = content.find("\"NAME\"", pos)) != std::string::npos) {
            size_t colon = content.find(':', pos);
            if (colon == std::string::npos) break;
            size_t q1 = content.find('"', colon);
            if (q1 == std::string::npos) break;
            size_t q2 = content.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            if (content.substr(q1 + 1, q2 - q1 - 1) == config_.account_name) {
                size_t lb = content.rfind('{', pos);
                if (lb == std::string::npos) break;
                int depth = 0;
                bool in_str = false;
                size_t rb = content.size();
                for (size_t i = lb; i < content.size(); i++) {
                    char ch = content[i];
                    if (in_str) {
                        if (ch == '\\') { i++; continue; }
                        if (ch == '"') in_str = false;
                        continue;
                    }
                    if (ch == '"') in_str = true;
                    else if (ch == '{') depth++;
                    else if (ch == '}') { depth--; if (depth == 0) { rb = i + 1; break; } }
                }
                begin = lb;
                end = rb;
                found_block = true;
                break;
            }
            pos = q2 + 1;
        }
        if (!found_block) {
            std::cerr << "[Config] 未找到账号 " << config_.account_name
                      << " 的配置块，跳过 YUANBAO_ID 回写\n";
            return;
        }
    }

    std::string block = content.substr(begin, end - begin);
    std::string new_block;
    size_t yp = block.find("\"YUANBAO_ID\"");
    if (yp != std::string::npos) {
        // 已有字段：替换其值
        size_t colon = block.find(':', yp);
        size_t q1 = (colon != std::string::npos) ? block.find('"', colon) : std::string::npos;
        size_t q2 = (q1 != std::string::npos) ? block.find('"', q1 + 1) : std::string::npos;
        if (q2 == std::string::npos) return;
        new_block = block.substr(0, q1 + 1) + config_.yuanbao_id + block.substr(q2);
    } else {
        // 无该字段：插入到 NAME 之后（保持 JSON 合法）
        size_t np = block.find("\"NAME\"");
        if (np == std::string::npos) return;
        size_t colon = block.find(':', np);
        size_t q1 = (colon != std::string::npos) ? block.find('"', colon) : std::string::npos;
        size_t q2 = (q1 != std::string::npos) ? block.find('"', q1 + 1) : std::string::npos;
        if (q2 == std::string::npos) return;
        size_t p = q2 + 1;
        while (p < block.size() &&
               (block[p] == ' ' || block[p] == '\t' || block[p] == '\r' || block[p] == '\n')) p++;
        if (p < block.size() && block[p] == ',') {
            // 原字段后已有逗号：插入的字段自身必须以逗号结尾
            new_block = block.substr(0, p + 1) + "\n      \"YUANBAO_ID\": \"" +
                        config_.yuanbao_id + "\"," + block.substr(p + 1);
        } else {
            new_block = block.substr(0, q2 + 1) + ",\n      \"YUANBAO_ID\": \"" +
                        config_.yuanbao_id + "\"" + block.substr(q2 + 1);
        }
    }

    content = content.substr(0, begin) + new_block + content.substr(end);
    std::ofstream f(path);
    if (f.is_open()) {
        f << content;
        f.close();
        std::cout << "[Config] YUANBAO_ID 已更新（账号 " << config_.account_name << "）\n";
    }
}

// =====================================================================
//  多群列表持久化（重启后不丢失）
// =====================================================================
void YuanbaoServer::persist_known_groups() {
    std::lock_guard<std::mutex> l(members_mu_);
    std::ostringstream o;
    o << "{\"groups\":[";
    bool first = true;
    for (auto& kv : known_groups_) {
        if (!first) o << ",";
        first = false;
        o << "{\"group_code\":\"" << kv.first << "\",\"group_name\":"
          << json_quote(kv.second) << "}";
    }
    o << "]}";
    std::ofstream f("logs/known_groups.json");
    if (f.is_open()) { f << o.str(); f.close(); }
}

void YuanbaoServer::load_known_groups() {
    std::ifstream f("logs/known_groups.json");
    if (!f.is_open()) return;
    std::stringstream ss; ss << f.rdbuf();
    JsonVal v;
    if (!json_parse(ss.str(), v)) return;
    std::lock_guard<std::mutex> l(members_mu_);
    for (auto& g : v["groups"].arr) {
        std::string gc = g["group_code"].asString();
        if (gc.empty()) continue;
        auto it = known_groups_.find(gc);
        if (it == known_groups_.end()) {
            known_groups_[gc] = g["group_name"].asString();
        } else if (it->second.empty()) {
            it->second = g["group_name"].asString();
        }
    }
}

// 自动获取监听群：不依赖 config 硬编码。
// 仅当 listen_groups 为空（用户未手动配置）时，自动把 bot 已加入的所有群设为监听；
// 一旦用户手动开关过（listen_groups 非空），保持用户的监听选择，不再全量覆盖。
void YuanbaoServer::auto_listen_from_known() {
    bool modified = false;
    {
        std::lock_guard<std::mutex> l(members_mu_);
        if (config_.listen_groups.empty()) {
            for (auto& kv : known_groups_) {
                if (!config_.is_listening(kv.first)) {
                    config_.listen_groups.push_back(kv.first);
                    modified = true;
                }
            }
        }
    }
    if (modified) save_config();
}

// =====================================================================
//  @全体成员 发送
// =====================================================================
bool YuanbaoServer::send_group_at_all(const std::string& group) {
    std::string text = "@\xE5\x85\xA8\xE4\xBD\x93\xE6\x88\x90\xE5\x91\x98"; // UTF-8 @全体成员
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body;
    bytes_append(body, proto::encode_string(1, body_msg_id));
    bytes_append(body, proto::encode_string(2, group));
    bytes_append(body, proto::encode_string(3, config_.yuanbao_id));
    bytes_append(body, proto::encode_string(4, ""));
    bytes_append(body, proto::encode_string(5, std::to_string((uint32_t)(std::chrono::steady_clock::now().time_since_epoch().count() % 4294967295))));
    // @全体消息需要特殊 at 标记
    Bytes at_elem;
    bytes_append(at_elem, proto::encode_string(1, "TIMTextElem"));
    Bytes at_content;
    bytes_append(at_content, proto::encode_string(1, text));
    bytes_append(at_content, proto::encode_string(2, "@all"));  // at 标记
    bytes_append(at_elem, proto::encode_message(2, at_content));
    bytes_append(body, proto::encode_message(6, at_elem));
    bytes_append(body, proto::encode_string(7, ""));
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    auto frame = proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
    return bot_ws_send_bytes(frame);
}

// =====================================================================
//  引用回复
// =====================================================================
bool YuanbaoServer::send_group_reply(const std::string& group, const std::string& text, const std::string& ref_msg_id,
                                     const std::string& at_user, const std::string& at_nick) {
    std::string body_msg_id = util::random_hex_id();
    std::string head_msg_id = util::random_hex_id();
    Bytes body = proto::encode_send_group_msg(body_msg_id, group, config_.yuanbao_id, text, ref_msg_id,
                                              at_user, at_nick);
    Bytes head = proto::encode_conn_head(0, "send_group_message", ++bot_seq_no_, head_msg_id, "yuanbao_openclaw_proxy");
    auto frame = proto::build_ws_frame(2, proto::encode_conn_msg(head, body));
    return bot_ws_send_bytes(frame);
}

// =====================================================================
//  消息缓存
// =====================================================================
void YuanbaoServer::cache_message(const JsonVal& msg) {
    std::lock_guard<std::mutex> l(msg_mu_);
    msg_cache_.push_back(msg);
    while (msg_cache_.size() > MAX_MSG_CACHE) msg_cache_.erase(msg_cache_.begin());
}

// =====================================================================
//  前端 WebSocket 广播
// =====================================================================
// =====================================================================
//  静态文件路径解析
// =====================================================================
// =====================================================================
//  API 路由

// =====================================================================
//  HTTP 路由
// =====================================================================

// 安全发送 HTTP 响应并在完成后关闭连接
// ← 修复：原先单次 send() 发送整个响应，大响应（如 base64 图片）可能被截断。
//   现改为循环发送直至全部送出（兼容部分系统/浏览器一次只收部分数据）。
// =====================================================================
//  WebSocket 路由（前端控制台）
// =====================================================================
// =====================================================================
//  Flood 实现
// =====================================================================
std::string YuanbaoServer::flood_start(const std::string& text, int count, int delay,
                                        int batch, const std::string& mode,
                                        const std::string& group) {
    (void)batch;
    flood::FloodMode fm = flood::FloodMode::RANDOM;
    std::string m2 = mode;
    std::transform(m2.begin(), m2.end(), m2.begin(), ::tolower);
    if (m2 == "fullwidth") fm = flood::FloodMode::FULLWIDTH;
    else if (m2 == "mock" || m2 == "random") fm = flood::FloodMode::MOCK;
    else if (m2 == "zalgo") fm = flood::FloodMode::ZALGO;
    else if (m2 == "repeat") fm = flood::FloodMode::REPEAT;
    else if (m2 == "alternate") fm = flood::FloodMode::ALTERNATE;
    else if (m2 == "emoji") fm = flood::FloodMode::EMOJI;
    else if (m2 == "ghost") fm = flood::FloodMode::GHOST;
    else if (m2 == "matrix") fm = flood::FloodMode::MATRIX;
    else if (m2 == "earthquake") fm = flood::FloodMode::EARTHQUAKE;
    else if (m2 == "bubble") fm = flood::FloodMode::BUBBLE;
    else if (m2 == "firework") fm = flood::FloodMode::FIREWORK;
    else if (m2 == "rainbow") fm = flood::FloodMode::RAINBOW;
    else if (m2 == "storm") fm = flood::FloodMode::STORM;
    else if (m2 == "tsunami") fm = flood::FloodMode::TSUNAMI;
    else if (m2 == "quantum") fm = flood::FloodMode::QUANTUM;

    flood::FloodTask task;
    task.text = text; task.count = count; task.delay_ms = delay;
    task.batch_size = batch; task.mode = fm; task.group_code = group;
    return flood_engine_.create_task(task);
}

void YuanbaoServer::flood_cancel(const std::string& id) { flood_engine_.cancel_task(id); }

std::string YuanbaoServer::flood_stats() {
    auto fs = flood_engine_.get_stats();
    std::ostringstream oss;
    oss << "{\"total_tasks\":" << fs.total_tasks
        << ",\"total_sent\":" << fs.total_sent
        << ",\"active_tasks\":" << fs.active_tasks << "}";
    return oss.str();
}

// =====================================================================
//  构造 / 析构
// =====================================================================
YuanbaoServer::YuanbaoServer() {}
YuanbaoServer::~YuanbaoServer() { stop(); }

bool YuanbaoServer::load_config(const std::string& path, const std::string& account, bool quiet) {
    config_path_ = path;
    if (!config_.load(path, account)) {
        if (!quiet)
            std::cerr << "[Config] 加载失败: " << path
                      << (account.empty() ? "" : ("  账号: " + account)) << "\n";
        return false;
    }
    if (!quiet) {
        std::cout << "[Config] OK";
        if (!config_.account_name.empty()) std::cout << "  账号: " << config_.account_name;
        std::cout << "\n";
        std::cout << "  AppKey: " << config_.app_key.substr(0, 8) << "...\n";
        std::cout << "  API: " << config_.api_domain << "\n";
        std::cout << "  WS: " << config_.ws_url << "\n";
        std::cout << "  默认目标群: " << config_.default_target() << "\n";
    }
    return true;
}

void YuanbaoServer::stop() {
    // 心跳线程必须回收（std::thread 未 join 会导致进程终止）
    hb_thread_started_ = false;
    {
        std::lock_guard<std::mutex> l(hb_mu_);
        hb_sessions_.clear();
    }
    if (hb_thread_.joinable()) hb_thread_.join();

    // 定时任务线程同样必须回收（先于 running_ 检查，一次性命令也会走到这里）
    task_thread_started_ = false;
    if (task_thread_.joinable()) task_thread_.join();

    if (!running_.load()) return;
    running_ = false;
    bot_running_ = false;
    msg_logger_.stop();
    if (bot_ws_thread_.joinable()) bot_ws_thread_.join();
    if (bot_ws_fd_ >= 0) { close_socket(bot_ws_fd_); bot_ws_fd_ = -1; }
    std::cout << "[Server] Stopped\n";
}

// =====================================================================
//  CLI（纯命令行）模式辅助实现
// =====================================================================

// 等待 auth-bind 鉴权完成（bot_connect 的线程收到 auth-bind 响应后置位）
bool YuanbaoServer::bot_wait_ready(int timeout_ms) {
    if (auth_response_received_.load()) return true;
    if (!bot_running_.load()) {
        std::cerr << "[CLI] Bot 未在运行\n";
        return false;
    }
    uint64_t deadline = util::now_ms() + (uint64_t)timeout_ms;
    while (util::now_ms() < deadline) {
        if (auth_response_received_.load()) return true;
        if (!bot_running_.load()) return false;
        if (g_exit_requested.load()) { std::cerr << "[CLI] 收到退出请求，取消等待鉴权\n"; return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cerr << "[CLI] 等待鉴权(auth-bind)超时 " << timeout_ms << " ms\n";
    return false;
}

// CLI 模式初始化：刷屏 sender / JSON 插件 / ico 图标映射 / 恢复已知群。
// 原来这些在 start()（Web 服务）里执行；纯命令行不再启动任何 HTTP/WebSocket 监听。
void YuanbaoServer::init_runtime() {
    flood_set_sender([this](const std::string& group, const std::string& text) {
        send_group_text(group, text);
    });
    load_known_groups();         // logs/known_groups.json → known_groups_
    scan_skills();               // skills/*/SKILL.md（OpenClaw / AgentSkills 兼容）
    mcp_init_all();              // MCP servers：initialize + tools/list
    task_load();                 // logs/tasks.json → 定时任务
    task_start();                // 启动调度线程（常驻命令与 listen 都需要）
}

// 同步查询群成员 / 群名 / 群主：发出两个查询帧，由 bot 接收线程解析回写缓存后返回。
bool YuanbaoServer::fetch_group_members_sync(
        const std::string& group,
        std::vector<std::pair<std::string, std::string>>& members,
        std::string& owner_id, std::string& group_name, int timeout_ms) {
    members.clear(); owner_id.clear(); group_name.clear();
    if (group.empty() || bot_ws_fd_ < 0) return false;

    std::string info_msg_id = util::random_hex_id();
    {
        std::lock_guard<std::mutex> l(members_mu_);
        members_response_ = false;
        group_info_response_ = false;
        members_request_group_ = group;
        pending_query_groups_[info_msg_id] = group;
    }
    if (!bot_ws_send_bytes(bot_ws_build_get_members_msg(group))) return false;
    if (!bot_ws_send_bytes(bot_ws_build_query_group_info_msg(group, info_msg_id))) return false;

    uint64_t deadline = util::now_ms() + (uint64_t)timeout_ms;
    while (util::now_ms() < deadline) {
        bool done;
        {
            std::lock_guard<std::mutex> l(members_mu_);
            done = members_response_ && group_info_response_;
        }
        if (done) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::lock_guard<std::mutex> l(members_mu_);
    auto it = members_cache_map_.find(group);
    if (it != members_cache_map_.end()) {
        members = it->second.members;
        owner_id = it->second.owner_id;
        group_name = it->second.group_name;
    }
    return !members.empty();
}

// =====================================================================
//  命令行工具实现（纯传参执行 · 无 Web / 无前端）
// =====================================================================
namespace cli {

using StrVec = std::vector<std::string>;

static StrVec utf8_argv(int argc, char** argv) {
    StrVec out;
#ifdef YUANBAO_PLATFORM_WINDOWS
    int n = 0;
    LPWSTR* wl = CommandLineToArgvW(GetCommandLineW(), &n);
    if (wl) {
        for (int i = 0; i < n; i++) out.push_back(util::wide_to_utf8(std::wstring(wl[i])));
        LocalFree(wl);
    }
#endif
    if (out.empty()) { for (int i = 0; i < argc; i++) out.push_back(argv[i]); }
    return out;
}

struct Options {
    StrVec pos;
    std::map<std::string, std::string> kv;
};

static Options parse_options(const StrVec& args, size_t from) {
    Options o;
    for (size_t i = from; i < args.size(); i++) {
        const std::string& a = args[i];
        if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
            std::string k = a.substr(2);
            bool has_next = (i + 1 < args.size());
            bool next_is_opt = has_next && args[i + 1].size() > 2 &&
                               args[i + 1][0] == '-' && args[i + 1][1] == '-';
            if (has_next && !next_is_opt) { o.kv[k] = args[++i]; }
            else o.kv[k] = "";
        } else {
            o.pos.push_back(a);
        }
    }
    return o;
}

static int opt_int(const Options& o, const std::string& k, int def) {
    auto it = o.kv.find(k);
    if (it == o.kv.end() || it->second.empty()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

// 把剩余位置参数拼成一段文本（空格分隔），方便不带引号连续输入
static std::string join_text(const StrVec& pos, size_t from) {
    std::string text;
    for (size_t i = from; i < pos.size(); i++) {
        if (!text.empty()) text += " ";
        text += pos[i];
    }
    return text;
}

// 群参数："default/默认" → config 默认目标群
static std::string resolve_group(const YuanbaoServer& s, const std::string& g) {
    if (g == "default" || g == "默认") return s.default_target();
    return g;
}

static bool is_url(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

// 用 curl 下载 URL 内容（用于"URL 发图"场景）
static bool http_download(const std::string& url, std::string& out) {
    if (!is_safe_url(url)) return false;   // 防命令注入
    std::ostringstream cmd;
    cmd << "curl -s -k --connect-timeout 10 -m 60 \"" << url << "\"";
    FILE* fp = popen(cmd.str().c_str(), "rb");
    if (!fp) return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
    int rc = pclose(fp);
    return rc == 0 && !out.empty();
}

static std::string basename_of(const std::string& path) {
    std::string b = path;
    size_t q = b.find('?');
    if (q != std::string::npos) b = b.substr(0, q);
    size_t s1 = b.find_last_of('/');
    size_t s2 = b.find_last_of('\\');
    size_t slash = (s1 == std::string::npos) ? s2 : (s2 == std::string::npos ? s1 : std::max(s1, s2));
    if (slash != std::string::npos) b = b.substr(slash + 1);
    return b;
}

// 确保已连接并完成 auth-bind 鉴权
static bool ensure_connected(YuanbaoServer& s) {
    if (s.app_key().empty() || s.api_domain().empty()) {
        std::cerr << "[失败] config.json 未配置 APP_KEY / API_DOMAIN，无法连接元宝服务器。\n";
        return false;
    }
    if (s.bot_is_connected() && s.bot_wait_ready(1000)) {
        std::cout << "[连接] 已在线\n";
        return true;
    }
    std::cout << "[连接] 正在连接元宝服务器...\n";
    s.bot_connect();
    if (!s.bot_wait_ready(20000)) {
        std::cerr << "[失败] 无法完成鉴权(auth-bind)，请检查 config.json 的 APP_KEY / APP_SECRET / 网络后重试。\n";
        return false;
    }
    std::cout << "[连接] 已连接并鉴权成功\n";
    return true;
}

// 一次性命令收尾：留出时间让帧发出/日志落盘，然后断开并清理
// 群回复发送成功后：对该会话发送 FINISH，并通知常驻 listen 进程停止 RUNNING 心跳
static void finish_group_heartbeat(YuanbaoServer& s, const std::string& group) {
    std::string acct = s.account_name().empty() ? std::string() : (s.account_name() + "_");
    std::string to;
    if (!util::read_file_utf8("logs/hb_active_" + acct + group, to)) return;
    while (!to.empty() && (to.back() == '\n' || to.back() == '\r' || to.back() == ' ')) to.pop_back();
    if (to.empty()) return;
    s.heartbeat_finish(true, group, to);
    std::ofstream f("logs/hb_stop_" + acct + group + "_" + to);
    if (f.is_open()) { f << "1"; f.close(); }
}

// 私聊回复发送成功后：发送 FINISH
static void finish_private_heartbeat(YuanbaoServer& s, const std::string& to) {
    if (!to.empty()) s.heartbeat_finish(false, "", to);
}

static int finish_oneshot(YuanbaoServer& s, int code) {
    // 等待后台任务（搜索/抓取/群查询/媒体上传/exec）跑完，最多 30 秒，
    // 否则一次性命令退出会把这些线程连带杀掉
    for (int i = 0; i < 100 && s.pending_tasks() > 0; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    s.bot_disconnect();
    s.stop();
    return code;
}

static std::string make_sticker_json(const std::string& id, const std::string& name) {
    JsonVal v;
    v["sticker_id"] = id;
    v["package_id"] = "1003";
    v["name"] = name;
    v["width"] = 128;
    v["height"] = 128;
    v["formats"] = "png";
    return json_compact(v);
}

// 贴纸名/模糊名/数字ID → (sticker_id, 展示名)；找不到返回 false
static bool resolve_sticker(const StickerLibrary& lib, const std::string& key,
                            std::string& id, std::string& name) {
    bool all_digit = !key.empty();
    for (char c : key) if (!(c >= '0' && c <= '9')) { all_digit = false; break; }
    if (all_digit) { id = key; name = "sticker"; return true; }
    id = lib.find_by_name(key);
    if (!id.empty()) { name = key; return true; }
    return false;
}

static void print_usage() {
    std::cout <<
"元宝 Bot CLI —— 纯命令行版（无 Web / 前端）\n"
"用法: main.exe <命令> [参数...] [--config 配置文件]\n"
"\n"
"连接 / 查询:\n"
"  connect                  连接测试（鉴权成功后退出）\n"
"  listen [--only-listen-groups] [--no-ai]\n"
"                           常驻监听：默认监听机器人所在的所有群，实时打印消息/事件；\n"
"                           被 @ 或私聊自动 AI 回复（详见下方 AI 回复段）；\n"
"                           --only-listen-groups 仅监听 config 的 LISTEN_GROUPS；Ctrl+C 退出\n"
"  groups                   列出本机已知群（logs/known_groups.json）\n"
"  members <群号>           查询群成员/群名/群主\n"
"  history [--group 群号] [--n 条数]   读取本地消息日志最近记录\n"
"  stickers                 列出全部内置贴纸\n"
"  status                   显示配置摘要\n"
"  slash <群号|dm:用户ID> <命令>   本地模拟斜杠命令（用于验证命令链路）\n"
"                           例: main.exe slash dm:用户ID /status\n"
"  rich <群号|dm:用户ID> <文本>    本地发送富文本（验证贴纸/@/刷屏解析）\n"
"                           例: main.exe rich 群号 \"[sticker:六六六] 大家\"\n"
"\n"
"发送（自动连接，发完即退出）:\n"
"  send-group <群号> <文本...>\n"
"  send-c2c <用户ID> <文本...>\n"
"  send-at <群号> <用户ID> <文本...> [--nick 昵称]\n"
"  send-multi-at <群号> <文本...> --users \"u1[:昵称1],u2[:昵称2],...\"\n"
"  send-at-all <群号>\n"
"  send-reply <群号> <被引用消息ID> <文本...> [--at 用户ID] [--at-nick 昵称]\n"
"  send-sticker <群号> <贴纸名或编号> [文本...] [--at 用户ID] [--at-nick 昵称]\n"
"              [--count 次数] [--interval 间隔毫秒]\n"
"  send-image <群号> <本地图片或 http(s)://URL> [--at 用户ID] [--at-nick 昵称]\n"
"  send-file <群号> <本地文件路径>\n"
"  send-video <群号> <本地视频文件路径>\n"
"\n"
"刷屏:\n"
"  flood <群号> <文本...> [--count 次数] [--interval 间隔毫秒] [--mode 变换]\n"
"        mode: random/fullwidth/mock/zalgo/repeat/alternate/emoji/ghost/matrix/\n"
"              earthquake/bubble/firework/rainbow/storm/tsunami/quantum\n"
"\n"
"AI 回复:\n"
"  ai-reply <群号> <用户ID> <文本...> [--nick 昵称] [--no-at] [--dry-run]\n"
"                           生成 AI 回复并发送到群（默认 @ 提问者；--dry-run 只预览）\n"
"  ai-c2c <用户ID> <文本...> [--dry-run]\n"
"                           生成 AI 回复并私聊发送\n"
"  ai-test <文本...>         只调用大模型打印回复（不发送，不需连接 Bot）\n"
"  listen                    常驻监听；被 @ 或收到私聊时自动 AI 回复\n"
"                            （配置了 LLM 即默认开启；--no-ai 可关闭）\n"
"  上下文记忆: 每个群/私聊保留最近 N 条消息（含 bot 自己的发言，默认 100 条），\n"
"             触发时随历史一起发给大模型；可用 config.json 的\n"
"             AI_CONTEXT_ENABLED / AI_CONTEXT_MESSAGES 调整（默认开启）\n"
"\n"
"AI 配置（config.json 全局或账号内）:\n"
"  LLM_API_URL / LLM_API_KEY / LLM_MODEL / LLM_SYSTEM_PROMPT /\n"
"  LLM_MAX_TOKENS / LLM_TEMPERATURE / LLM_TIMEOUT\n"
"  亦可临时覆盖: --llm-api-url --llm-api-key --llm-model --llm-system\n"
"                --llm-max-tokens --llm-temperature\n"
"  示例: main.exe ai-test \"你好\" --account main\n"
"        main.exe ai-reply 群号 用户ID \"今天天气如何\" --account main\n"
"\n"
"全局选项（--account 必填）:\n"
"  --config <路径>              指定配置文件（默认 config.json）\n"
"  --account <名称|序号|all>    指定 Bot 账号（必填）；\n"
"                               all = 对全部账号依次执行（listen 时并行监听）\n"
"\n"
"多账号配置（config.json，每组 APP_KEY/APP_SECRET 都必须有 NAME）:\n"
"  \"ACCOUNTS\": [ { \"NAME\": \"main\", \"APP_KEY\": \"...\", \"APP_SECRET\": \"...\",\n"
"                   \"LISTEN_GROUPS\": [\"群号\"], \"YUANBAO_ID\": \"\" } ]\n"
"  示例: main.exe send-group 群号 \"你好\" --account main\n"
"        main.exe status --account main\n"
"        main.exe listen --account all\n"
"\n"
"斜杠命令（在元宝客户端发送，listen 常驻时生效）:\n"
"  会话:   /help /status /new /reset /stop /compact /retry\n"
"  信息:   /time /uptime /version /ping /whoami /calc /echo /commands\n"
"  群/历史: /groups /groupinfo /members /history /inspect\n"
"  定时/媒体: /remind /cron /sticker /stickers /img /file /reply\n"
"  运维:   /restart /yuanbao-upgrade /yuanbaobot-upgrade /issue-log\n"
"  连接成功时命令清单会自动同步到元宝平台；群聊需 @机器人，私聊直接可用。\n"
"  /new 清空当前会话上下文  /stop 停止正在生成的回复  /compact 压缩上下文\n"
"\n"
"说明: 群号可为 default（使用 config 的默认目标群）；含空格文本请加英文引号。\n";
}

// ─────────────────────────── 命令实现 ───────────────────────────

static int cmd_status(const YuanbaoServer& s) {
    std::cout << "配置摘要:\n";
    std::cout << "  默认目标群(default_target): " << (s.default_target().empty() ? "(无)" : s.default_target()) << "\n";
    std::cout << "  已知群数量: " << s.known_groups_.size() << "\n";
    std::cout << "  连接状态: " << (s.bot_is_connected() ? "在线" : "未连接") << "\n";
    std::cout << "  内置贴纸数量: " << s.stickers_.stickers.size() << "\n";
    return 0;
}

static int cmd_connect(YuanbaoServer& s) {
    if (!ensure_connected(s)) return 1;
    std::cout << "[OK] 连接测试通过\n";
    return finish_oneshot(s, 0);
}

static int cmd_groups(const YuanbaoServer& s) {
    if (s.known_groups_.empty()) {
        std::cout << "暂无已知群。可通过 listen 收到群消息、或配置 LISTEN_GROUPS 后运行\n"
                     "groups 刷新。历史记录见 logs/known_groups.json。\n";
        return 0;
    }
    std::cout << "已知群（来自 logs/known_groups.json 与历史消息）:\n";
    for (auto& kv : s.known_groups_) {
        std::string tag = (kv.first == s.default_target()) ? "  ← 默认目标" : "";
        std::cout << "  " << kv.first
                  << (kv.second.empty() ? "" : "  " + kv.second)
                  << tag << "\n";
    }
    return 0;
}

static int cmd_members(YuanbaoServer& s, const Options& o) {
    if (o.pos.empty()) { std::cerr << "用法: main.exe members <群号>\n"; return 1; }
    std::string g = resolve_group(s, o.pos[0]);
    if (g.empty()) { std::cerr << "[错误] 群号为空（未配置默认目标群时需显式给出群号）\n"; return 1; }
    if (!ensure_connected(s)) return 1;

    std::vector<std::pair<std::string, std::string>> members;
    std::string owner, gname;
    if (!s.fetch_group_members_sync(g, members, owner, gname)) {
        std::cerr << "[失败] 查询群 " << g << " 失败或返回为空（超时）\n";
        return finish_oneshot(s, 1);
    }
    std::cout << "群: " << g << (gname.empty() ? "" : " (" + gname + ")")
              << "  成员数: " << members.size() << "\n";
    if (!owner.empty()) std::cout << "群主: " << owner << "\n";
    for (auto& m : members) {
        std::cout << "  " << m.first << (m.second.empty() ? "" : "  " + m.second) << "\n";
    }
    return finish_oneshot(s, 0);
}

static int cmd_history(const Options& o) {
    std::string fg = o.kv.count("group") ? o.kv.at("group") : "";
    int n = opt_int(o, "n", 20);
    if (n <= 0) n = 20;
    if (n > 2000) n = 2000;

    std::vector<std::string> files;
#ifdef YUANBAO_PLATFORM_WINDOWS
    std::wstring pat = L"./logs/messages_*.log";
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(pat.c_str(), &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do { files.push_back(util::wide_to_utf8(ffd.cFileName)); } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }
#else
    DIR* d = opendir("./logs");
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d))) {
            std::string n0 = ent->d_name;
            if (n0.rfind("messages_", 0) == 0 && n0.size() > 4 &&
                n0.compare(n0.size() - 4, 4, ".log") == 0) files.push_back(n0);
        }
        closedir(d);
    }
#endif
    std::sort(files.begin(), files.end());

    std::vector<std::string> picked;
    for (auto& fn : files) {
        std::ifstream f("./logs/" + fn);
        if (!f.is_open()) continue;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            JsonVal v;
            if (!json_parse(line, v)) continue;
            if (!fg.empty() && v["group_code"].asString() != fg) continue;
            picked.push_back(line);
            if ((int)picked.size() > n) picked.erase(picked.begin());
        }
    }
    if (picked.empty()) { std::cout << "暂无历史消息。\n"; return 0; }
    for (auto& line : picked) {
        JsonVal v;
        if (!json_parse(line, v)) continue;
        bool c2c = v["is_c2c"].asBool();
        std::string where = c2c ? ("私聊 " + v["c2c_peer"].asString())
                                : ("群 " + v["group_code"].asString());
        std::cout << "[" << v["timestamp"].asString() << "] " << where
                  << "  " << v["sender_name"].asString() << "(" << v["sender_id"].asString()
                  << "): " << v["content"].asString() << "\n";
    }
    return 0;
}

static int cmd_stickers(const YuanbaoServer& s) {
    if (s.stickers_.stickers.empty()) { std::cout << "暂无贴纸。\n"; return 0; }
    std::cout << "内置贴纸 " << s.stickers_.stickers.size() << " 个（可用名称或编号发送）:\n";
    std::string line;
    int col = 0;
    for (auto& kv : s.stickers_.stickers) {
        std::ostringstream cell;
        cell << "  " << kv.first << "=" << kv.second;
        line += cell.str();
        if (++col == 3) { std::cout << line << "\n"; line.clear(); col = 0; }
    }
    if (!line.empty()) std::cout << line << "\n";
    return 0;
}

// 本地模拟"平台下发斜杠命令"，用于验证 /new /stop 等命令链路（不依赖真实平台消息）
static int cmd_slash(YuanbaoServer& s, const Options& o) {
    if (o.pos.size() < 2) {
        std::cerr << "用法: main.exe slash <群号|dm:用户ID> <命令> [参数...]\n"
                     "示例: main.exe slash dm:用户ID /status\n"
                     "      main.exe slash 群号 \"/new\"\n";
        return 1;
    }
    std::string target = o.pos[0];
    std::string text = join_text(o.pos, 1);
    std::string arg, scmd = parse_slash_command(text, arg);
    if (scmd.empty()) {
        std::cerr << "[错误] 不是受支持的斜杠命令: " << text
                  << "（支持 /help /status /new /reset /stop /compact /retry）\n";
        return 1;
    }
    bool is_c2c = target.rfind("dm:", 0) == 0;
    std::string group = is_c2c ? std::string() : resolve_group(s, target);
    std::string peer = is_c2c ? target.substr(3) : std::string();
    if (!is_c2c && group.empty()) { std::cerr << "[错误] 群号为空\n"; return 1; }
    if (is_c2c && peer.empty()) { std::cerr << "[错误] 私聊用户ID为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    s.handle_slash_command(is_c2c, group, is_c2c ? peer : std::string("local"), "本地模拟", scmd, arg);
    std::cout << "[OK] 命令已执行: " << scmd << "\n";
    // /yuanbao-upgrade 在独立线程编译（约 30 秒）：等待脚本生成，避免一次性命令提前退出
    if (scmd == "/yuanbao-upgrade" || scmd == "/yuanbaobot-upgrade") {
        std::cout << "[等待] 升级流程执行中（编译约 30 秒）...\n";
        uint64_t deadline = util::now_ms() + 180000;
        while (util::now_ms() < deadline) {
            if (FILE* f = fopen("upgrade.bat", "rb")) { fclose(f); break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    return finish_oneshot(s, 0);
}

// 本地发送富文本：直接走 AI 回复的解析（[sticker:名] / [flood:文本|次数|模式|间隔] / @昵称）
static int cmd_rich(YuanbaoServer& s, const Options& o) {
    if (o.pos.size() < 2) {
        std::cerr << "用法: main.exe rich <群号|dm:用户ID> <文本...> [--ref 被引用消息ID]\n"
                     "  [sticker:贴纸名] 发贴纸；[flood:文本|次数|模式|间隔ms] 刷屏；@昵称 转真实 @\n"
                     "  --ref <msg_id> 以引用回复方式发送（消息ID 可从 logs/messages_*.log 取）\n"
                     "  例: main.exe rich 群号 \"[sticker:六六六] 大家好\"\n"
                     "      main.exe rich 群号 \"收到\" --ref <消息ID>\n";
        return 1;
    }
    std::string target = o.pos[0];
    std::string text = join_text(o.pos, 1);
    bool is_c2c = target.rfind("dm:", 0) == 0;
    std::string group = is_c2c ? std::string() : resolve_group(s, target);
    std::string peer = is_c2c ? target.substr(3) : std::string();
    if (!is_c2c && group.empty()) { std::cerr << "[错误] 群号为空\n"; return 1; }
    if (is_c2c && peer.empty()) { std::cerr << "[错误] 私聊用户ID为空\n"; return 1; }
    if (text.empty()) { std::cerr << "[错误] 文本为空\n"; return 1; }
    std::string ref = o.kv.count("ref") ? o.kv.at("ref") : "";
    // 本地测试：以 OWNER_ID（未配置则 "local"）身份执行，便于验证 [exec:...] 等高风险指令
    std::string sid = s.owner_id_value().empty() ? std::string("local") : s.owner_id_value();
    if (!ensure_connected(s)) return 1;
    bool ok = s.send_ai_reply(is_c2c, group, peer, text, std::string(), std::string(), ref, sid);
    // 含刷屏指令时：刷屏是后台任务，必须等它跑完再断开连接（否则会发出失败）
    bool has_flood = text.find("[flood:") != std::string::npos ||
                     text.find("[刷屏:") != std::string::npos;
    if (ok && has_flood) {
        std::cout << "[等待] 刷屏任务执行中...\n";
        uint64_t deadline = util::now_ms() + 120000;
        while (util::now_ms() < deadline) {
            JsonVal st;
            if (json_parse(s.flood_stats(), st) && st["active_tasks"].asInt() == 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_listen(YuanbaoServer& s, const Options& o) {
    // 监听范围：默认监听机器人所在的所有群；--only-listen-groups 仅监听 config.LISTEN_GROUPS
    if (o.kv.count("only-listen-groups") > 0) {
        s.set_listen_all_groups(false);
        std::cout << "[监听] 范围：仅 config.json 的 LISTEN_GROUPS\n";
    } else {
        s.set_listen_all_groups(true);
        std::cout << "[监听] 范围：机器人所在的所有群（--only-listen-groups 可限制）\n";
    }
    // AI 自动回复：默认启用（已配置 LLM 时），被 @ 或私聊消息触发；--no-ai 关闭
    bool no_ai = o.kv.count("no-ai") > 0;
    if (no_ai) {
        s.set_ai_auto_reply(false);
        std::cout << "[监听] AI 自动回复已关闭（--no-ai）\n";
    } else if (s.llm_configured()) {
        s.set_ai_auto_reply(true);
        std::cout << "[监听] AI 自动回复已启用：被 @ 或私聊消息会自动回复（--no-ai 可关闭）\n";
    } else {
        std::cout << "[监听] 未配置 AI（LLM_API_URL / LLM_API_KEY），被 @ 时不会自动回复；"
                     "可用 --llm-api-* 参数临时指定\n";
    }
    if (!ensure_connected(s)) return 1;
    // 上次进程遗留的"思考中"气泡（被强杀/断线时未收尾）在这里补发 FINISH 清掉
    s.heartbeat_cleanup_stale();
    std::cout << "[监听] 已进入常驻模式，收到的消息/事件将实时打印；Ctrl+C 退出。\n";
    while (!g_exit_requested.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "\n[监听] 收到退出信号，正在收尾思考气泡...\n";
    s.heartbeat_finish_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    s.bot_disconnect();
    std::cout << "[监听] 已退出。\n";
    return 0;
}

static int cmd_send_group(YuanbaoServer& s, const Options& o) {
    if (o.pos.empty()) { std::cerr << "用法: main.exe send-group <群号> <文本...>\n"; return 1; }
    std::string g = resolve_group(s, o.pos[0]);
    std::string text = join_text(o.pos, 1);
    if (g.empty() || text.empty()) { std::cerr << "[错误] 群号或文本为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    std::cout << "[发送] 群 " << g << ":\n  " << text << "\n";
    bool ok = s.send_group_text(g, text);
    if (ok) finish_group_heartbeat(s, g);
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败（连接可能已断开）\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_send_c2c(YuanbaoServer& s, const Options& o) {
    if (o.pos.empty()) { std::cerr << "用法: main.exe send-c2c <用户ID> <文本...>\n"; return 1; }
    std::string to = o.pos[0];
    std::string text = join_text(o.pos, 1);
    if (to.empty() || text.empty()) { std::cerr << "[错误] 用户ID 或文本为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    std::cout << "[发送] 私聊 " << to << ":\n  " << text << "\n";
    bool ok = s.send_c2c_text(to, text);
    if (ok) finish_private_heartbeat(s, to);
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_send_at(YuanbaoServer& s, const Options& o) {
    if (o.pos.size() < 2) { std::cerr << "用法: main.exe send-at <群号> <用户ID> <文本...> [--nick 昵称]\n"; return 1; }
    std::string g = resolve_group(s, o.pos[0]);
    std::string uid = o.pos[1];
    std::string text = join_text(o.pos, 2);
    std::string nick = o.kv.count("nick") ? o.kv.at("nick") : "";
    if (g.empty() || uid.empty()) { std::cerr << "[错误] 群号/用户ID 为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    std::cout << "[发送] @" << (nick.empty() ? uid : nick) << " 群 " << g
              << (text.empty() ? "" : ":\n  " + text) << "\n";
    bool ok = s.send_group_at(g, text, uid, nick);
    if (ok) finish_group_heartbeat(s, g);
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_send_multi_at(YuanbaoServer& s, const Options& o) {
    if (o.pos.empty() || !o.kv.count("users")) {
        std::cerr << "用法: main.exe send-multi-at <群号> <文本...> --users \"u1[:昵称1],u2[:昵称2],...\"\n";
        return 1;
    }
    std::string g = resolve_group(s, o.pos[0]);
    std::string text = join_text(o.pos, 1);
    std::vector<std::pair<std::string, std::string>> at_users;
    std::string list = o.kv.at("users");
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!item.empty()) {
            size_t colon = item.find(':');
            if (colon != std::string::npos) at_users.push_back({item.substr(0, colon), item.substr(colon + 1)});
            else at_users.push_back({item, ""});
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (g.empty() || at_users.empty()) { std::cerr << "[错误] 群号或 @ 用户列表为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    std::string desc;
    for (auto& u : at_users) desc += "@" + (u.second.empty() ? u.first : u.second) + " ";
    std::cout << "[发送] " << desc << " 群 " << g << (text.empty() ? "" : ":\n  " + text) << "\n";
    bool ok = s.send_group_multi_at(g, text, at_users);
    if (ok) finish_group_heartbeat(s, g);
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_send_at_all(YuanbaoServer& s, const Options& o) {
    if (o.pos.empty()) { std::cerr << "用法: main.exe send-at-all <群号>\n"; return 1; }
    std::string g = resolve_group(s, o.pos[0]);
    if (g.empty()) { std::cerr << "[错误] 群号为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    std::cout << "[发送] @全体成员 到群 " << g << "\n";
    bool ok = s.send_group_at_all(g);
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_send_reply(YuanbaoServer& s, const Options& o) {
    if (o.pos.size() < 2) {
        std::cerr << "用法: main.exe send-reply <群号> <被引用消息ID> <文本...> [--at 用户ID] [--at-nick 昵称]\n";
        return 1;
    }
    std::string g = resolve_group(s, o.pos[0]);
    std::string ref = o.pos[1];
    std::string text = join_text(o.pos, 2);
    std::string atu = o.kv.count("at") ? o.kv.at("at") : "";
    std::string atn = o.kv.count("at-nick") ? o.kv.at("at-nick") : "";
    if (g.empty() || ref.empty() || text.empty()) { std::cerr << "[错误] 群号/引用ID/文本均不能为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    std::cout << "[发送] 引用回复 " << ref << " 群 " << g << ":\n  " << text << "\n";
    bool ok = s.send_group_reply(g, text, ref, atu, atn);
    if (ok) finish_group_heartbeat(s, g);
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_send_sticker(YuanbaoServer& s, const Options& o) {
    if (o.pos.size() < 2) {
        std::cerr << "用法: main.exe send-sticker <群号> <贴纸名或编号> [文本...] [--at 用户ID] [--at-nick 昵称]\n"
                     "      [--count 次数] [--interval 间隔毫秒]\n";
        return 1;
    }
    std::string g = resolve_group(s, o.pos[0]);
    std::string key = o.pos[1];
    std::string text = join_text(o.pos, 2);
    std::string atu = o.kv.count("at") ? o.kv.at("at") : "";
    std::string atn = o.kv.count("at-nick") ? o.kv.at("at-nick") : "";
    int count = opt_int(o, "count", 1);
    int interval = opt_int(o, "interval", 500);
    if (count < 1) count = 1;
    std::string sid, sname;
    if (!resolve_sticker(s.stickers_, key, sid, sname)) {
        std::cerr << "[错误] 找不到贴纸: " << key << "（用 stickers 命令查看全部）\n";
        return 1;
    }
    if (g.empty()) { std::cerr << "[错误] 群号为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    std::string sj = make_sticker_json(sid, sname);
    std::cout << "[发送] 贴纸 " << sname << "(" << sid << ") 到群 " << g
              << (count > 1 ? (" x" + std::to_string(count)) : "") << "\n";
    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        ok = s.send_group_sticker(g, sj, atu, atn, text);
        if (i + 1 < count) std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
    if (ok) finish_group_heartbeat(s, g);
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static bool send_media_upload(YuanbaoServer& s, const std::string& g, const std::string& src,
                              const std::string& atu, const std::string& atn,
                              const std::string& media_type) {
    std::string data;
    std::string base = src;
    if (is_url(src)) {
        std::cout << "[下载] " << src << "\n";
        if (!http_download(src, data) || data.empty()) {
            std::cerr << "[错误] URL 下载失败（需要 curl，或网络不可达）\n";
            return false;
        }
        base = basename_of(src);
    } else {
        if (!util::read_file_utf8(src, data)) {
            std::cerr << "[错误] 无法读取本地文件: " << src << "\n";
            return false;
        }
        base = basename_of(src);
    }
    if (data.empty()) { std::cerr << "[错误] 文件内容为空\n"; return false; }
    if (data.size() > 20 * 1024 * 1024) { std::cerr << "[错误] 文件超过 20MB 限制\n"; return false; }
    if (base.empty() || base.size() > 128) base = (media_type == "image") ? "image.png" : "file.bin";

    std::cout << "[上传] " << base << " (" << data.size() << " 字节)...\n";
    std::string url, uuid;
    if (!s.upload_media(base, data, url, uuid)) {
        std::cerr << "[错误] 媒体上传(COS)失败\n";
        return false;
    }
    if (media_type == "image") {
        int w = 0, h = 0;
        util::parse_image_dimensions(data, w, h);
        bool ok = s.send_group_image(g, url, uuid, (int)data.size(), w, h, atu, atn);
        if (ok) std::cout << "[OK] 图片已发送到群 " << g << "\n";
        else std::cerr << "[失败] 图片消息发送失败\n";
        return ok;
    }
    bool ok = s.send_group_file(g, url, base, uuid, (int)data.size(), atu, atn);
    if (ok) std::cout << "[OK] " << (media_type == "video" ? "视频" : "文件")
                      << "已发送到群 " << g << "\n";
    else std::cerr << "[失败] 文件消息发送失败\n";
    return ok;
}

static int cmd_send_media(YuanbaoServer& s, const Options& o, const std::string& media_type) {
    if (o.pos.size() < 2) {
        std::cerr << "用法: main.exe " << media_type << " <群号> <本地文件或 http(s)://URL>\n";
        return 1;
    }
    std::string g = resolve_group(s, o.pos[0]);
    std::string src = o.pos[1];
    std::string atu = o.kv.count("at") ? o.kv.at("at") : "";
    std::string atn = o.kv.count("at-nick") ? o.kv.at("at-nick") : "";
    if (g.empty() || src.empty()) { std::cerr << "[错误] 群号/文件为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;
    bool ok = send_media_upload(s, g, src, atu, atn, media_type);
    if (ok) finish_group_heartbeat(s, g);
    return finish_oneshot(s, ok ? 0 : 1);
}

static int cmd_flood(YuanbaoServer& s, const Options& o) {
    if (o.pos.empty()) {
        std::cerr << "用法: main.exe flood <群号> <文本...> [--count 次数] [--interval 毫秒] [--mode 变换]\n";
        return 1;
    }
    std::string g = resolve_group(s, o.pos[0]);
    std::string text = join_text(o.pos, 1);
    int count = opt_int(o, "count", 10);
    int interval = opt_int(o, "interval", 50);
    std::string mode = o.kv.count("mode") ? o.kv.at("mode") : "random";
    if (count < 1) count = 1;
    if (interval < 1) interval = 1;
    if (g.empty() || text.empty()) { std::cerr << "[错误] 群号/文本为空\n"; return 1; }
    if (!ensure_connected(s)) return 1;

    std::cout << "[刷屏] 群 " << g << " 次数=" << count
              << " 间隔=" << interval << "ms mode=" << mode << "\n";
    std::string id = s.flood_start(text, count, interval, 1, mode, g);
    std::cout << "[刷屏] 任务已启动 id=" << id << "，等待完成...\n";

    uint64_t deadline = util::now_ms() + (uint64_t)count * (interval + 60) + 15000;
    int64_t last = 0;
    while (util::now_ms() < deadline) {
        JsonVal st;
        if (json_parse(s.flood_stats(), st)) {
            int active = st["active_tasks"].asInt();
            int sent = st["total_sent"].asInt();
            if (active == 0) break;
            if (sent != last) { last = sent; std::cout << "  已发送 " << sent << "/" << count << "\r" << std::flush; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    JsonVal st;
    json_parse(s.flood_stats(), st);
    std::cout << "\n[刷屏] 完成，累计发送 " << st["total_sent"].asInt() << " 条。\n";
    finish_group_heartbeat(s, g);
    return finish_oneshot(s, 0);
}

// ── AI 回复命令 ──

// 未配置 LLM 时给出明确提示
static bool require_llm(const YuanbaoServer& s) {
    if (s.llm_configured()) return true;
    std::cerr << "[错误] 未配置 AI：请在 config.json（全局或账号内）添加\n"
                 "       LLM_API_URL / LLM_API_KEY / LLM_MODEL，\n"
                 "       或临时使用 --llm-api-url / --llm-api-key / --llm-model 参数\n";
    return false;
}

// ai-test <文本...>：只调用大模型并打印回复（不发送、不需要 Bot 连接）
static int cmd_ai_test(YuanbaoServer& s, const Options& o) {
    std::string text = join_text(o.pos, 0);
    if (text.empty()) {
        std::cerr << "用法: main.exe ai-test <文本...> [--account X]\n";
        return 1;
    }
    if (!require_llm(s)) return 1;
    std::cout << "[AI] 请求中: " << text.substr(0, 60) << "\n";
    std::string reply = s.ai_generate(text, "测试用户");
    if (reply.empty()) { std::cerr << "[失败] 未获得回复（检查 API 配置与网络）\n"; return 1; }
    std::cout << "[AI] " << reply << "\n";
    return 0;
}

// ai-reply <群号> <用户ID> <文本...> [--nick 昵称] [--no-at]
// 生成 AI 回复并发送到群（默认 @ 提问者）
static int cmd_ai_reply(YuanbaoServer& s, const Options& o) {
    if (o.pos.size() < 3) {
        std::cerr << "用法: main.exe ai-reply <群号> <用户ID> <文本...> "
                     "[--nick 昵称] [--no-at] [--account X]\n";
        return 1;
    }
    std::string g = resolve_group(s, o.pos[0]);
    std::string uid = o.pos[1];
    std::string text = join_text(o.pos, 2);
    std::string nick = o.kv.count("nick") ? o.kv.at("nick") : "";
    bool no_at = o.kv.count("no-at") > 0;
    if (g.empty() || uid.empty() || text.empty()) {
        std::cerr << "[错误] 群号 / 用户ID / 文本不能为空\n";
        return 1;
    }
    if (!require_llm(s)) return 1;

    std::cout << "[AI] 生成回复中（含会话上下文）...\n";
    std::string reply = s.ai_generate_ctx(false, g, "", text, nick.empty() ? uid : nick, uid);
    if (reply.empty()) { std::cerr << "[失败] AI 未返回内容\n"; return 1; }


    if (o.kv.count("dry-run") > 0) {
        std::cout << "[AI] (dry-run，未发送) " << reply << "\n";
        return 0;
    }

    if (!ensure_connected(s)) return 1;
    // 走富文本发送：支持 [sticker:名称] 贴纸；未指定 --no-at 时默认 @ 提问者
    bool ok = s.send_ai_reply(false, g, uid, reply,
                              no_at ? std::string() : uid,
                              no_at ? std::string() : (nick.empty() ? uid : nick),
                              std::string(), uid);
    if (ok) finish_group_heartbeat(s, g);
    std::cout << "[AI] " << reply.substr(0, 120) << "\n";
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

// ai-c2c <用户ID> <文本...>：生成 AI 回复并私聊发送
static int cmd_ai_c2c(YuanbaoServer& s, const Options& o) {
    if (o.pos.size() < 2) {
        std::cerr << "用法: main.exe ai-c2c <用户ID> <文本...> [--account X]\n";
        return 1;
    }
    std::string uid = o.pos[0];
    std::string text = join_text(o.pos, 1);
    if (uid.empty() || text.empty()) { std::cerr << "[错误] 用户ID / 文本不能为空\n"; return 1; }
    if (!require_llm(s)) return 1;

    std::cout << "[AI] 生成回复中（含会话上下文）...\n";
    std::string reply = s.ai_generate_ctx(true, "", uid, text, uid, uid);
    if (reply.empty()) { std::cerr << "[失败] AI 未返回内容\n"; return 1; }


    if (o.kv.count("dry-run") > 0) {
        std::cout << "[AI] (dry-run，未发送) " << reply << "\n";
        return 0;
    }

    if (!ensure_connected(s)) return 1;
    // 走富文本发送：支持 [sticker:名称] 贴纸
    bool ok = s.send_ai_reply(true, "", uid, reply,
                              std::string(), std::string(), std::string(), uid);
    if (ok) finish_private_heartbeat(s, uid);
    std::cout << "[AI] " << reply.substr(0, 120) << "\n";
    std::cout << (ok ? "[OK] 已发送\n" : "[失败] 发送失败\n");
    return finish_oneshot(s, ok ? 0 : 1);
}

static int run(YuanbaoServer& s, const StrVec& args) {
    if (args.size() < 2) { print_usage(); return 1; }
    const std::string& cmd = args[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h" || cmd == "/?") {
        print_usage();
        return 0;
    }
    Options o = parse_options(args, 2);

    if (cmd == "status")                    return cmd_status(s);
    if (cmd == "connect")                   return cmd_connect(s);
    if (cmd == "listen")                    return cmd_listen(s, o);
    if (cmd == "groups")                    return cmd_groups(s);
    if (cmd == "members")                   return cmd_members(s, o);
    if (cmd == "history")                   return cmd_history(o);
    if (cmd == "slash" || cmd == "cmd")     return cmd_slash(s, o);
    if (cmd == "rich")                      return cmd_rich(s, o);
    if (cmd == "stickers" || cmd == "sticker-list") return cmd_stickers(s);
    if (cmd == "send-group" || cmd == "send" || cmd == "sg") return cmd_send_group(s, o);
    if (cmd == "send-c2c" || cmd == "dm" || cmd == "sc")     return cmd_send_c2c(s, o);
    if (cmd == "send-at" || cmd == "sat")                    return cmd_send_at(s, o);
    if (cmd == "send-multi-at" || cmd == "smat")             return cmd_send_multi_at(s, o);
    if (cmd == "send-at-all" || cmd == "at-all")             return cmd_send_at_all(s, o);
    if (cmd == "send-reply" || cmd == "sr")                  return cmd_send_reply(s, o);
    if (cmd == "send-sticker" || cmd == "sticker")           return cmd_send_sticker(s, o);
    if (cmd == "send-image" || cmd == "image" || cmd == "img") return cmd_send_media(s, o, "image");
    if (cmd == "send-file" || cmd == "file")                 return cmd_send_media(s, o, "file");
    if (cmd == "send-video" || cmd == "video")               return cmd_send_media(s, o, "video");
    if (cmd == "flood" || cmd == "flood-text")               return cmd_flood(s, o);
    if (cmd == "ai-test")                                    return cmd_ai_test(s, o);
    if (cmd == "ai-reply" || cmd == "ai")                    return cmd_ai_reply(s, o);
    if (cmd == "ai-c2c")                                     return cmd_ai_c2c(s, o);

    std::cerr << "未知命令: " << cmd << "\n";
    print_usage();
    return 1;
}

} // namespace cli

// =====================================================================
//  main —— 纯命令行入口（无 Web / 前端）
// =====================================================================
int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 源码字符串为 UTF-8，Windows 控制台默认 GBK(CP936) 会显示乱码，切换到 UTF-8 代码页
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 关闭 stdout/stderr 缓冲，保证输出顺序与实时刷新
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif

    cli::StrVec args = cli::utf8_argv(argc, argv);

    std::cout << "元宝 Bot CLI (C++17, 纯命令行版)  ——  main.exe <命令> [参数] [--config 配置]\n";

    if (args.size() < 2) {
        cli::print_usage();
        return 1;
    }

    // 初始化网络 / OpenSSL
    if (!yb_platform::net_init()) {
        std::cerr << "FATAL: Network init failed\n";
        return 1;
    }
#ifdef YUANBAO_NO_OPENSSL
    std::cout << "  [INFO] OpenSSL disabled - Bot 连接(WSS)不可用，仅本地命令可用\n";
#else
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
#endif

    // 全局选项：--config / --account / --llm-*
    std::string config_path = "config.json";
    std::string account_sel;
    std::string llm_url, llm_key, llm_model, llm_system;
    int llm_max_tokens = 0;
    double llm_temp = -1;
    for (size_t i = 2; i + 1 < args.size(); i++) {
        if (args[i] == "--config") { config_path = args[i + 1]; i++; }
        else if (args[i] == "--account") { account_sel = args[i + 1]; i++; }
        else if (args[i] == "--llm-api-url") { llm_url = args[i + 1]; i++; }
        else if (args[i] == "--llm-api-key") { llm_key = args[i + 1]; i++; }
        else if (args[i] == "--llm-model") { llm_model = args[i + 1]; i++; }
        else if (args[i] == "--llm-system") { llm_system = args[i + 1]; i++; }
        else if (args[i] == "--llm-max-tokens") { llm_max_tokens = std::atoi(args[i + 1].c_str()); i++; }
        else if (args[i] == "--llm-temperature") { llm_temp = std::atof(args[i + 1].c_str()); i++; }
    }
    auto apply_llm_override = [&](YuanbaoServer& srv) {
        if (!llm_url.empty() || !llm_key.empty() || !llm_model.empty() || !llm_system.empty()
            || llm_max_tokens > 0 || llm_temp >= 0) {
            srv.set_llm_override(llm_url, llm_key, llm_model, llm_system, llm_max_tokens, llm_temp);
        }
    };

    const std::string cmd = (args.size() > 1) ? args[1] : "";
    const bool is_listen = (cmd == "listen");

    // 枚举 config 中的账号（用于 --account 校验与错误提示）
    YuanbaoServer probe;
    probe.load_config(config_path, "", true);
    std::vector<std::string> all_names = probe.account_names();

    // 除 help 外，必须显式指定 --account
    if (cmd != "help" && account_sel.empty()) {
        std::cerr << "[错误] 必须使用 --account 指定账号";
        if (!all_names.empty()) {
            std::cerr << "（可用: ";
            for (size_t k = 0; k < all_names.size(); k++)
                std::cerr << (k ? ", " : "") << all_names[k];
            std::cerr << "；或 all）";
        }
        std::cerr << "\n       用法: main.exe <命令> [参数...] --account <名称|序号|all>\n";
        yb_platform::net_cleanup();
        return 1;
    }

    // 目标账号列表：all → 全部账号；否则单个
    bool want_all = (account_sel == "all" || account_sel == "ALL" || account_sel == "*");
    std::vector<std::string> targets;
    if (want_all) {
        targets = all_names;
        if (targets.empty()) {
            std::cerr << "[错误] config 中没有任何账号（请在 ACCOUNTS 中配置 NAME/APP_KEY/APP_SECRET）\n";
            yb_platform::net_cleanup();
            return 1;
        }
    } else {
        targets.push_back(account_sel);
    }

    // Ctrl+C / Ctrl+Break：只置位退出请求（不再直接 exit），
    // 由常驻命令 listen 先收尾"思考中"气泡再退出；一次性命令在检查点退出
    signal(SIGINT, [](int) { g_exit_requested = true; });
#ifndef _WIN32
    signal(SIGTERM, [](int) { g_exit_requested = true; });
#endif

    auto run_account = [&](const std::string& acct) -> int {
        YuanbaoServer server;
        if (!server.load_config(config_path, acct)) {
            std::cerr << "[错误] 无法加载账号 " << acct
                      << "（请检查 config.json 的 ACCOUNTS / NAME 配置）\n";
            return 1;
        }
        server.init_runtime();   // 纯命令行运行时初始化（不再启动任何 Web/HTTP 服务）
        apply_llm_override(server);
        {
            std::string warn = server.exec_startup_warning();
            if (!warn.empty()) std::cout << warn;
        }
        return cli::run(server, args);
    };

    int rc = 0;
    if (targets.size() <= 1) {
        rc = run_account(targets.empty() ? "" : targets[0]);
    } else if (is_listen) {
        // 多账号并行监听：每个账号一个独立连接的实例
        std::cout << "[多账号] 并行监听 " << targets.size() << " 个账号\n";
        std::vector<std::unique_ptr<YuanbaoServer>> servers;
        std::vector<std::thread> threads;
        for (auto& a : targets) {
            auto sp = std::make_unique<YuanbaoServer>();
            if (!sp->load_config(config_path, a)) {
                std::cerr << "[提示] 账号加载失败，跳过: " << a << "\n";
                continue;
            }
            sp->init_runtime();
            apply_llm_override(*sp);
            {
                std::string warn = sp->exec_startup_warning();
                if (!warn.empty()) std::cout << warn;
            }
            servers.push_back(std::move(sp));
        }
        for (auto& sp : servers) {
            YuanbaoServer* p = sp.get();
            threads.emplace_back([p, &args]() { cli::run(*p, args); });
        }
        for (auto& t : threads) t.join();
    } else {
        // 多账号：依次对每个账号执行同一命令
        for (auto& a : targets) {
            std::cout << "\n===== 账号: " << (a.empty() ? std::string("default") : a) << " =====\n";
            int r = run_account(a);
            if (r != 0) rc = r;
        }
    }

    yb_platform::net_cleanup();
    return rc;
}
