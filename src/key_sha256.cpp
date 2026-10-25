#include "key_sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mobileautotune {
namespace {
constexpr std::array<uint32_t, 64> kRound{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
uint32_t Rotate(uint32_t value, unsigned amount) {
  return value >> amount | value << (32 - amount);
}
void Compress(const unsigned char* block, std::array<uint32_t, 8>& state) {
  std::array<uint32_t, 64> words{};
  for (int i = 0; i < 16; ++i) {
    words[i] = static_cast<uint32_t>(block[4 * i]) << 24 |
               static_cast<uint32_t>(block[4 * i + 1]) << 16 |
               static_cast<uint32_t>(block[4 * i + 2]) << 8 | block[4 * i + 3];
  }
  for (int i = 16; i < 64; ++i) {
    uint32_t a = words[i - 15], b = words[i - 2];
    words[i] = words[i - 16] + (Rotate(a, 7) ^ Rotate(a, 18) ^ (a >> 3)) +
               words[i - 7] + (Rotate(b, 17) ^ Rotate(b, 19) ^ (b >> 10));
  }
  auto working = state;
  for (int i = 0; i < 64; ++i) {
    uint32_t e = working[4], a = working[0];
    uint32_t first =
        working[7] + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) +
        ((e & working[5]) ^ (~e & working[6])) + kRound[i] + words[i];
    uint32_t second =
        (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) +
        ((a & working[1]) ^ (a & working[2]) ^ (working[1] & working[2]));
    for (int j = 7; j > 0; --j) {
      working[j] = working[j - 1];
    }
    working[4] += first;
    working[0] = first + second;
  }
  for (int i = 0; i < 8; ++i) {
    state[i] += working[i];
  }
}
}  // namespace

std::string KeyFileSha256(const std::string& path,
                          const std::atomic<bool>* canceled) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot read key model");
  }
  std::array<uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, 65536> buffer{};
  uint64_t bytes = 0;
  while (file) {
    if (canceled && canceled->load()) {
      throw std::runtime_error("Analysis canceled");
    }
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    size_t count = file.gcount();
    bytes += count;
    size_t full = count / 64 * 64;
    for (size_t i = 0; i < full; i += 64) {
      Compress(buffer.data() + i, state);
    }
    if (count < buffer.size()) {
      if (!file.eof()) {
        throw std::runtime_error("Cannot read key model");
      }
      std::array<unsigned char, 128> tail{};
      std::copy_n(buffer.data() + full, count - full, tail.data());
      tail[count - full] = 0x80;
      size_t blocks = count - full < 56 ? 64 : 128;
      uint64_t bits = bytes * 8;
      for (int i = 0; i < 8; ++i) {
        tail[blocks - 1 - i] = bits >> (8 * i);
      }
      Compress(tail.data(), state);
      if (blocks == 128) {
        Compress(tail.data() + 64, state);
      }
      break;
    }
  }
  std::ostringstream output;
  for (uint32_t word : state) {
    output << std::hex << std::setfill('0') << std::setw(8) << word;
  }
  return output.str();
}
}  // namespace mobileautotune
