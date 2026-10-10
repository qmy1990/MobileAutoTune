#include "mat/offline.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace {
using File = std::unique_ptr<FILE, decltype(&fclose)>;
uint16_t Read16(const unsigned char* p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t Read32(const unsigned char* p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
void Write16(FILE* f, uint16_t v) {
  unsigned char p[]{static_cast<unsigned char>(v),
                    static_cast<unsigned char>(v >> 8)};
  fwrite(p, 1, 2, f);
}
void Write32(FILE* f, uint32_t v) {
  Write16(f, v & 65535);
  Write16(f, v >> 16);
}
void WriteHeader(FILE* f, uint32_t bytes) {
  fwrite("RIFF", 1, 4, f);
  Write32(f, 36 + bytes);
  fwrite("WAVEfmt ", 1, 8, f);
  Write32(f, 16);
  Write16(f, 1);
  Write16(f, 1);
  Write32(f, 48000);
  Write32(f, 96000);
  Write16(f, 2);
  Write16(f, 16);
  fwrite("data", 1, 4, f);
  Write32(f, bytes);
}
struct Wav {
  int channels = 0, bits = 0, format = 0, align = 0;
  uint32_t bytes = 0;
  long data = 0;
};
bool ParseWav(FILE* f, Wav& w) {
  std::array<unsigned char, 40> p{};
  if (fread(p.data(), 1, 12, f) != 12 || memcmp(p.data(), "RIFF", 4) ||
      memcmp(p.data() + 8, "WAVE", 4)) {
    return false;
  }
  if (fseek(f, 0, SEEK_END)) {
    return false;
  }
  long length = ftell(f);
  if (length < 12) {
    return false;
  }
  if (fseek(f, 12, SEEK_SET)) {
    return false;
  }
  bool fmt = false, data = false;
  while (ftell(f) + 8 <= length) {
    if (fread(p.data(), 1, 8, f) != 8) {
      return false;
    }
    uint32_t size = Read32(p.data() + 4);
    long start = ftell(f);
    if (uint64_t(start) + size > uint64_t(length)) {
      return false;
    }
    if (!memcmp(p.data(), "fmt ", 4)) {
      if (size < 16 || fread(p.data(), 1, 16, f) != 16) {
        return false;
      }
      w.format = Read16(p.data());
      w.channels = Read16(p.data() + 2);
      w.align = Read16(p.data() + 12);
      w.bits = Read16(p.data() + 14);
      if (Read32(p.data() + 4) != 48000) {
        return false;
      }
      if (w.format == 0xfffe) {
        if (size < 40 || fread(p.data() + 16, 1, 24, f) != 24) {
          return false;
        }
        static constexpr unsigned char kSubformatSuffix[]{
            0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113};
        if (Read16(p.data() + 16) < 22 || Read16(p.data() + 18) != w.bits ||
            memcmp(p.data() + 26, kSubformatSuffix, 14)) {
          return false;
        }
        w.format = Read16(p.data() + 24);
      }
      if ((w.channels != 1 && w.channels != 2) ||
          !((w.format == 1 && w.bits == 16) ||
            (w.format == 3 && w.bits == 32)) ||
          w.align != w.channels * w.bits / 8) {
        return false;
      }
      fmt = true;
    } else if (!memcmp(p.data(), "data", 4) && !data) {
      w.data = start;
      w.bytes = size;
      data = true;
    }
    long next = start + size + (size & 1);
    if (next > length) {
      break;
    }
    if (fseek(f, next, SEEK_SET)) {
      return false;
    }
  }
  return fmt && data && w.bytes % w.align == 0 && !fseek(f, w.data, SEEK_SET);
}
}  // namespace
extern "C" int mat_process_wav(const char* input, const char* output,
                               const mat_parameters* p, mat_mode mode, int tail,
                               mat_progress_callback progress, void* user) {
  if (!input || !output || !p || !*input || !*output) {
    return -1;
  }
  std::string temporary;
  try {
    std::error_code ec;
    if (std::filesystem::equivalent(input, output, ec)) {
      return -1;
    }
    if (std::filesystem::absolute(input).lexically_normal() ==
        std::filesystem::absolute(output).lexically_normal()) {
      return -1;
    }
    File in(fopen(input, "rb"), fclose);
    if (!in) {
      return -3;
    }
    Wav wav;
    if (!ParseWav(in.get(), wav)) {
      return -2;
    }
    std::unique_ptr<mat_engine, decltype(&mat_destroy)> engine(
        mat_create(48000, mode), mat_destroy);
    if (!engine) {
      return -4;
    }
    if (mat_set_parameters(engine.get(), p)) {
      return -1;
    }
    mat_reset(engine.get());
    uint64_t input_frames = wav.bytes / wav.align;
    uint64_t output_frames =
        input_frames + (tail ? mat_tail_samples(engine.get()) : 0);
    if (output_frames > (UINT32_MAX - 36) / 2) {
      return -2;
    }
    temporary = std::string(output) + ".tmp-XXXXXX";
    std::vector<char> name(temporary.begin(), temporary.end());
    name.push_back(0);
    int fd = mkstemp(name.data());
    if (fd < 0) {
      return -3;
    }
    temporary = name.data();
    File out(fdopen(fd, "wb"), fclose);
    if (!out) {
      close(fd);
      remove(temporary.c_str());
      return -3;
    }
    WriteHeader(out.get(), uint32_t(output_frames * 2));
    uint64_t latency = mat_latency_samples(engine.get()),
             total = output_frames + latency, written = 0;
    std::array<float, 480> x{}, y{};
    std::array<unsigned char, 480 * 8> encoded{};
    int error = 0;
    for (uint64_t offset = 0; offset < total; offset += 480) {
      x.fill(0);
      int count = int(std::min<uint64_t>(
          480, offset < input_frames ? input_frames - offset : 0));
      if (count &&
          fread(encoded.data(), wav.align, count, in.get()) != size_t(count)) {
        error = -2;
        break;
      }
      for (int i = 0; i < count; ++i) {
        float value = 0;
        for (int ch = 0; ch < wav.channels; ++ch) {
          auto b = encoded.data() + i * wav.align + ch * wav.bits / 8;
          if (wav.format == 1) {
            value += int16_t(Read16(b)) / 32768.f;
          } else {
            uint32_t bits = Read32(b);
            float v;
            memcpy(&v, &bits, 4);
            value += std::isfinite(v) ? v : 0;
          }
        }
        x[i] = value / wav.channels;
      }
      mat_process_frame(engine.get(), x.data(), y.data());
      int bytes = 0;
      for (int i = 0; i < 480; ++i) {
        uint64_t position = offset + i;
        if (position >= latency && position < total) {
          int sample = int(
              std::lrint(std::max(-1.f, std::min(0.999969f, y[i])) * 32768.f));
          encoded[bytes++] = sample & 255;
          encoded[bytes++] = (sample >> 8) & 255;
          ++written;
        }
      }
      if (bytes &&
          fwrite(encoded.data(), 1, bytes, out.get()) != size_t(bytes)) {
        error = -3;
        break;
      }
      if (progress && (offset % 24000 == 0 || offset + 480 >= total) &&
          progress(float(std::min(1.0, double(offset + 480) / total)), user)) {
        error = -5;
        break;
      }
    }
    if (written != output_frames && !error) {
      error = -3;
    }
    if (fflush(out.get()) || ferror(out.get())) {
      error = -3;
    }
    FILE* closing = out.release();
    if (fclose(closing)) {
      error = -3;
    }
    if (!error && rename(temporary.c_str(), output)) {
      error = -3;
    }
    if (error) {
      remove(temporary.c_str());
    }
    return error;
  } catch (...) {
    if (!temporary.empty()) {
      remove(temporary.c_str());
    }
    return -4;
  }
}
