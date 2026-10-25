#ifndef MOBILEAUTOTUNE_SRC_KEY_SHA256_HPP_
#define MOBILEAUTOTUNE_SRC_KEY_SHA256_HPP_
#include <atomic>
#include <string>
namespace mobileautotune {
std::string KeyFileSha256(const std::string& path,
                          const std::atomic<bool>* canceled = nullptr);
}
#endif  // MOBILEAUTOTUNE_SRC_KEY_SHA256_HPP_
