#ifndef SHA1_H
#define SHA1_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>

class SHA1 {
private:
    uint32_t digest[5];
    std::string buffer;
    uint64_t transforms;

    void reset() {
        digest[0] = 0x67452301;
        digest[1] = 0xefcdab89;
        digest[2] = 0x98badcfe;
        digest[3] = 0x10325476;
        digest[4] = 0xc3d2e1f0;
        buffer.clear();
        transforms = 0;
    }

    static uint32_t rol(uint32_t value, uint32_t bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    static uint32_t blk(uint32_t block[16], uint32_t i) {
        return (block[i & 15] =
            rol(block[(i + 13) & 15] ^ block[(i + 8) & 15] ^
                block[(i + 2) & 15] ^ block[i & 15], 1));
    }

    static void R0(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x,
                   uint32_t y, uint32_t &z, uint32_t i) {
        z += ((w & (x ^ y)) ^ y) + block[i] + 0x5a827999 + rol(v, 5);
        w = rol(w, 30);
    }
    static void R1(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x,
                   uint32_t y, uint32_t &z, uint32_t i) {
        z += ((w & (x ^ y)) ^ y) + blk(block, i) + 0x5a827999 + rol(v, 5);
        w = rol(w, 30);
    }
    static void R2(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x,
                   uint32_t y, uint32_t &z, uint32_t i) {
        z += (w ^ x ^ y) + blk(block, i) + 0x6ed9eba1 + rol(v, 5);
        w = rol(w, 30);
    }
    static void R3(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x,
                   uint32_t y, uint32_t &z, uint32_t i) {
        z += (((w | x) & y) | (w & x)) + blk(block, i) + 0x8f1bbcdc + rol(v, 5);
        w = rol(w, 30);
    }
    static void R4(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x,
                   uint32_t y, uint32_t &z, uint32_t i) {
        z += (w ^ x ^ y) + blk(block, i) + 0xca62c1d6 + rol(v, 5);
        w = rol(w, 30);
    }

    void transform() {
        uint32_t block[16];
        for (size_t i = 0; i < 16; i++) {
            block[i] = (static_cast<uint8_t>(buffer[4 * i + 3]) & 0xff)
                     | (static_cast<uint8_t>(buffer[4 * i + 2]) & 0xff) << 8
                     | (static_cast<uint8_t>(buffer[4 * i + 1]) & 0xff) << 16
                     | (static_cast<uint8_t>(buffer[4 * i + 0]) & 0xff) << 24;
        }
        uint32_t a = digest[0], b = digest[1], c = digest[2], d = digest[3], e = digest[4];

        R0(block, a, b, c, d, e, 0);   R0(block, e, a, b, c, d, 1);
        R0(block, d, e, a, b, c, 2);   R0(block, c, d, e, a, b, 3);
        R0(block, b, c, d, e, a, 4);   R0(block, a, b, c, d, e, 5);
        R0(block, e, a, b, c, d, 6);   R0(block, d, e, a, b, c, 7);
        R0(block, c, d, e, a, b, 8);   R0(block, b, c, d, e, a, 9);
        R0(block, a, b, c, d, e, 10);  R0(block, e, a, b, c, d, 11);
        R0(block, d, e, a, b, c, 12);  R0(block, c, d, e, a, b, 13);
        R0(block, b, c, d, e, a, 14);  R0(block, a, b, c, d, e, 15);
        R1(block, e, a, b, c, d, 16);  R1(block, d, e, a, b, c, 17);
        R1(block, c, d, e, a, b, 18);  R1(block, b, c, d, e, a, 19);
        R2(block, a, b, c, d, e, 20);  R2(block, e, a, b, c, d, 21);
        R2(block, d, e, a, b, c, 22);  R2(block, c, d, e, a, b, 23);
        R2(block, b, c, d, e, a, 24);  R2(block, a, b, c, d, e, 25);
        R2(block, e, a, b, c, d, 26);  R2(block, d, e, a, b, c, 27);
        R2(block, c, d, e, a, b, 28);  R2(block, b, c, d, e, a, 29);
        R2(block, a, b, c, d, e, 30);  R2(block, e, a, b, c, d, 31);
        R2(block, d, e, a, b, c, 32);  R2(block, c, d, e, a, b, 33);
        R2(block, b, c, d, e, a, 34);  R2(block, a, b, c, d, e, 35);
        R2(block, e, a, b, c, d, 36);  R2(block, d, e, a, b, c, 37);
        R2(block, c, d, e, a, b, 38);  R2(block, b, c, d, e, a, 39);
        R3(block, a, b, c, d, e, 40);  R3(block, e, a, b, c, d, 41);
        R3(block, d, e, a, b, c, 42);  R3(block, c, d, e, a, b, 43);
        R3(block, b, c, d, e, a, 44);  R3(block, a, b, c, d, e, 45);
        R3(block, e, a, b, c, d, 46);  R3(block, d, e, a, b, c, 47);
        R3(block, c, d, e, a, b, 48);  R3(block, b, c, d, e, a, 49);

        digest[0] += a;
        digest[1] += b;
        digest[2] += c;
        digest[3] += d;
        digest[4] += e;
    }

public:
    SHA1() { reset(); }

    void update(const std::string &s) {
        buffer += s;
        while (buffer.size() >= 64) {
            transform();
            transforms++;
            buffer.erase(0, 64);
        }
    }

    void update(std::istream &is) {
        char sbuf[4096];
        while (is.read(sbuf, sizeof(sbuf))) {
            update(std::string(sbuf, sizeof(sbuf)));
        }
        if (is.gcount() > 0) update(std::string(sbuf, static_cast<size_t>(is.gcount())));
    }

    std::string final() {
        uint64_t total_bits = (transforms * 64 + buffer.size()) * 8;
        buffer.push_back(static_cast<char>(0x80));
        while ((buffer.size() % 64) != 56) buffer.push_back(static_cast<char>(0x00));
        for (int i = 0; i < 8; i++) buffer.push_back(static_cast<char>((total_bits >> (56 - i * 8)) & 0xff));

        while (buffer.size() >= 64) {
            transform();
            transforms++;
            buffer.erase(0, 64);
        }

        std::ostringstream result;
        for (size_t i = 0; i < 5; i++) result << std::hex << std::setfill('0') << std::setw(8) << digest[i];
        std::string out = result.str();
        reset();
        return out;
    }

    static std::string from_file(const std::string &filename) {
        std::ifstream stream(filename.c_str(), std::ios::binary);
        if (!stream) return "";
        SHA1 checksum;
        checksum.update(stream);
        return checksum.final();
    }
};

#endif // SHA1_H
