#pragma once

#include <string>
#include <chrono>
#include <random>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace NeonSight {

inline std::string GenerateUUIDv7() {
  auto now = std::chrono::system_clock::now();
  uint64_t ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

  static thread_local std::mt19937_64 rng(std::random_device{}());
  std::uniform_int_distribution<uint64_t> dist;

  uint64_t rand_a = dist(rng) & 0x0FFFull;             // 12 random bits
  uint64_t rand_b = dist(rng) & 0x3FFFFFFFFFFFFFFFull; // 62 random bits

  // Layout:
  //   [0-3]  top 32 bits of ms timestamp
  //   [4-5]  next 16 bits of ms
  //   [6-7]  ver(7) | rand_a (12 bits)
  //   [8-9]  var(2b10) | top 14 bits of rand_b
  //   [10-15] remaining 48 bits of rand_b

  uint32_t ts_high = (uint32_t)(ms >> 16);
  uint16_t ts_low = (uint16_t)(ms & 0xFFFFull);
  uint16_t ver_ra = (uint16_t)(0x7000u | rand_a);
  uint16_t var_rb_hi = (uint16_t)(0x8000u | (uint16_t)((rand_b >> 48) & 0x3FFFu));
  uint64_t rb_lo = rand_b & 0x0000FFFFFFFFFFFFull;

  std::ostringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(8) << ts_high << '-' << std::setw(4) << ts_low << '-' << std::setw(4) << ver_ra << '-'
     << std::setw(4) << var_rb_hi << '-' << std::setw(12) << rb_lo;
  return ss.str();
}

} // namespace NeonSight
