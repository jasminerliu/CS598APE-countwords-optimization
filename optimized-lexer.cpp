#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <memory>
#include <utility>
#include <omp.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string_view>
#include <sys/stat.h>
#include <vector>
#include <arm_neon.h>

enum type { whitespace, word, number, punctuation, other };
constexpr std::array<std::string_view, 5> names = {
    "whitespace", "word", "number", "punctuation", "other"};

bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool word_start(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128; }
bool word_character(unsigned char c) { return word_start(c) || digit(c); }
bool space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

inline uint8x16_t in_range(uint8x16_t bytes, unsigned char first, unsigned char last) {
  return vandq_u8(vcgeq_u8(bytes, vdupq_n_u8(first)), vcleq_u8(bytes, vdupq_n_u8(last)));
}

// Store four bits per byte to quickly find where matching characters end
inline std::uint64_t mask(uint8x16_t matches) {
  return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(matches), 4)), 0);
}

class lexer {
  std::string_view input;
  type current = type::other;
  std::size_t position = 0;
  bool pending = false;
  std::array<std::uint64_t, names.size()> counts{};

  void count_one() {
    const auto index = static_cast<std::size_t>(current);
    ++counts[index];
    pending = false;
  }

public:
  explicit lexer(std::string_view source) : input(source) {}

  // Process next character, tracking which token it belongs to
  void push(char byte) {
    const auto i = position++;
    const auto c = static_cast<unsigned char>(byte);
    const auto next = i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) : 0;
    if (pending) {
      if (current == type::whitespace && space(c)) return;
      if (current == type::word) {
        if (word_character(c)) return;
        // Treat ' and - as part of word when another char follows
        if ((c == '\'' || c == '-') && word_character(next)) return;
      }
      if (current == type::number) {
        if (digit(c)) return;
        if ((c == '.' || c == ',') && digit(next)) return;
      }
      count_one();
    }
    pending = true;
    if (space(c)) current = type::whitespace;
    else if (word_start(c)) current = type::word;
    else if (digit(c) || (c == '.' && digit(next))) current = type::number;
    else {
      current = c >= 33 && c <= 126 ? type::punctuation : type::other;
      count_one();
    }
  }

  // Classify 16 bytes at once, using masks to skip matching characters together
  void scan(std::size_t end) {
    while (end - position >= 16) {
      const auto bytes = vld1q_u8(reinterpret_cast<const unsigned char*>(input.data() + position));
      const auto lower = vorrq_u8(bytes, vdupq_n_u8(0x20));
      const auto start_mask = mask(vorrq_u8(in_range(lower, 'a', 'z'), vcgeq_u8(bytes, vdupq_n_u8(128))));
      const auto digit_mask = mask(in_range(bytes, '0', '9'));
      const auto word_mask = start_mask | digit_mask;
      const auto space_mask = mask(vorrq_u8(vceqq_u8(bytes, vdupq_n_u8(' ')), in_range(bytes, '\t', '\r')));
      const auto slashes = mask(vorrq_u8(vceqq_u8(bytes, vdupq_n_u8('\'')), vceqq_u8(bytes, vdupq_n_u8('-'))));
      const auto periods = mask(vceqq_u8(bytes, vdupq_n_u8('.')));
      const auto commas = periods | mask(vceqq_u8(bytes, vdupq_n_u8(',')));
      const auto misc_ascii = mask(in_range(bytes, 33, 126));
      const auto next = position + 16 < input.size() ? static_cast<unsigned char>(input[position + 16]) : 0;
      const auto next_words = (word_mask >> 4) | (word_character(next) ? UINT64_C(0xf000000000000000) : 0);
      const auto next_digits = (digit_mask >> 4) | (digit(next) ? UINT64_C(0xf000000000000000) : 0);
      const auto word_run = word_mask | (slashes & next_words);
      const auto number_run = digit_mask | (commas & next_digits);
      const auto number_start = digit_mask | (periods & next_digits);

      std::size_t offset = 0;
      while (offset < 16) {
        const auto bit = UINT64_C(1) << (offset * 4);
        if (!pending) {
          if (space_mask & bit) current = type::whitespace;
          else if (start_mask & bit) current = type::word;
          else if (number_start & bit) current = type::number;
          else {
            ++counts[misc_ascii & bit ? type::punctuation : type::other];
            ++offset;
            continue;
          }
          pending = true;
          ++offset;
          if (offset == 16) break;
        }
        const auto matches = current == type::word ? word_run : current == type::number ? number_run : space_mask;
        const auto remaining = ~(matches >> (offset * 4));
        offset += remaining ? static_cast<std::size_t>(__builtin_ctzll(remaining) / 4) : 16 - offset;
        if (offset < 16) count_one();
      }
      position += 16;
    }
    while (position < end) push(input[position]);
  }

  void finish() { if (pending) count_one(); }

  // Start new chunk without resetting tokens already counted
  void start_chunk(std::size_t begin) {
    position = begin;
    pending = false;
  }

  void merge(const lexer& worker) {
    for (std::size_t i = 0; i < names.size(); ++i) counts[i] += worker.counts[i];
  }

  void dump(std::ostream& out) {
    for (std::size_t i = 0; i < names.size(); ++i) out << names[i] << ' ' << counts[i] << '\n';
  }
};

// Same env settings as optimized-buffered.cpp
std::size_t setting(const char* name, std::size_t fallback) {
  const char* value = std::getenv(name);
  if (!value) return fallback;
  std::string_view text(value);
  std::size_t number = 0;
  std::from_chars(text.data(), text.data() + text.size(), number);
  return number;
}

int main() {
  std::ios::sync_with_stdio(false);
  const auto num_threads = static_cast<std::size_t>(omp_get_max_threads());
  const auto chunk_size = setting("CHUNK_SIZE", 1024 * 1024);
  // Same input setup as optimized-buffered.cpp
  std::vector<char> input;
  struct stat info;
  if (fstat(fileno(stdin), &info) == 0 && S_ISREG(info.st_mode)) {
    const auto position = ftello(stdin);
    if (position >= 0 && info.st_size > position) {
      const auto remaining = static_cast<std::uintmax_t>(info.st_size - position);
      if (remaining <= input.max_size()) input.reserve(static_cast<std::size_t>(remaining));
    }
  }
  char block[64 * 1024];
  std::size_t size;
  while ((size = std::fread(block, 1, sizeof(block), stdin)) != 0)
    input.insert(input.end(), block, block + size);

  const auto source = input.empty() ? std::string_view{} : std::string_view(input.data(), input.size());
  lexer counts(source);

  // Move chunk end past the next token to keep them together
  std::vector<std::pair<std::size_t, std::size_t>> chunks;
  for (std::size_t begin = 0; begin < input.size();) {
    auto end = begin + std::min(chunk_size, input.size() - begin);
    while (end < input.size() && !space(static_cast<unsigned char>(input[end]))) ++end;
    while (end < input.size() && space(static_cast<unsigned char>(input[end]))) ++end;
    chunks.emplace_back(begin, end);
    begin = end;
  }

  const int workers = static_cast<int>(std::min(num_threads, chunks.size()));
  std::vector<std::unique_ptr<lexer>> local;
  for (int i = 0; i < workers; ++i) local.push_back(std::make_unique<lexer>(source));

  // Same OpenMP structure as optimized-buffered.cpp
  #pragma omp parallel for num_threads(std::max(1, workers)) schedule(dynamic, 1) if(workers > 1)
  for (std::size_t chunk = 0; chunk < chunks.size(); ++chunk) {
    auto& local_counts = *local[omp_get_thread_num()];
    auto [begin, end] = chunks[chunk];
    local_counts.start_chunk(begin);
    local_counts.scan(end);
    local_counts.finish();
  }

  for (auto& local_counts : local) counts.merge(*local_counts);
  counts.dump(std::cout);
  std::cout.flush();
}
