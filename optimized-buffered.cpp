#include <algorithm>
#include <cinttypes>
#include <cassert>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <charconv>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <omp.h>
#include <string_view>
#include <sys/stat.h>


template <std::size_t hash_size, std::size_t words_buf_size>
class table {
  enum : std::uint64_t { hash_init = 0xcbf29ce484222325ULL };
  enum : std::uint64_t { hash_next = 0x100000001b3ULL };
  struct range { int place; std::uint16_t sz; }; // { offset into words, size }
  struct cell { int count = 0; range word; };    // a hash entry

  std::uint64_t hash = hash_init;
  char* tail = words; char* back = words;
  std::size_t entries = 0;
  cell buckets[hash_size];
  char words[words_buf_size];

  auto word_at(range w) { return std::string_view{words + w.place, w.sz}; }
public:
  void push(char c) {
    assert(std::size_t(back - words) < sizeof(words));
    *back++ = c, hash = hash * hash_next ^ c;
  }
  void count_one(int amount = 1) {
    if (back != tail) {
      std::string_view  key{tail, std::size_t(back-tail)};
      auto match = [key,this](range word) {
        return word.sz == key.size() && word_at(word) == key; };
      auto* bucket = &buckets[hash % hash_size]; // op& if hash_size is 2^n
      for (; bucket->count && !match(bucket->word);
        bucket = (bucket == buckets) ? buckets+hash_size-1 : bucket-1) {}
      if (! bucket->count) {
        assert(++entries < hash_size/2);
        range word = {int(tail - words), std::uint16_t(back - tail)};
        *bucket = {amount, word }, tail = back;  // keep new word
      } else bucket->count += amount, back = tail;  // discard word
      hash = hash_init;
    }
  }
  // Combine worker tables by adding each word's total count
  void merge(table& other) {
    for (auto& bucket : other.buckets) {
      if (!bucket.count) continue;
      for (char c : other.word_at(bucket.word)) push(c);
      count_one(bucket.count);
    }
  }

  void dump(std::ostream& out) {
    auto end = std::partition(buckets, buckets + hash_size, [](auto& b){
      return b.count != 0; });
    std::sort(buckets, end, [](cell& a, cell& b) {
      return a.count > b.count; });
    std::for_each(buckets, end, [&](cell& b) {
      out << word_at(b.word) << ' ' << b.count << '\n'; });
  }
};

// Read env vars
std::size_t setting(const char* name, std::size_t fallback) {
  const char* value = std::getenv(name);
  if (!value) return fallback;
  std::string_view text(value);
  std::size_t number = 0;
  std::from_chars(text.data(), text.data() + text.size(), number);
  return number;
}


table<(1<<16), (1<<19)> counts;

int main() {
  std::ios::sync_with_stdio(false);
  // Fix bug: convert only ASCII uppercase letters
  auto to_lower = [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
  };
  const auto num_threads = static_cast<std::size_t>(omp_get_max_threads());
  const auto chunk_size = setting("CHUNK_SIZE", 1024 * 1024);

  // Read entire file into preallocated buffer, avoiding memory copies
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
  if (std::ferror(stdin)) return 1;

  // Extend chunk to next separator so word is never split between chunks
  std::vector<std::pair<std::size_t, std::size_t>> chunks;
  for (std::size_t begin = 0; begin < input.size();) {
    auto end = begin + std::min(chunk_size, input.size() - begin);
    while (end < input.size() && static_cast<unsigned char>(input[end]) > ' ') ++end;
    chunks.emplace_back(begin, end);
    begin = end;
  }

  using counts_table = table<(1<<16), (1<<19)>;
  const int workers = static_cast<int>(std::min(num_threads, chunks.size()));

  std::vector<std::unique_ptr<counts_table>> local;
  for (int i = 0; i < workers; ++i) local.push_back(std::make_unique<counts_table>());

  // Workers count words in their own tables
  #pragma omp parallel for num_threads(std::max(1, workers)) schedule(dynamic, 1) if(workers > 1)
  for (std::size_t chunk = 0; chunk < chunks.size(); ++chunk) {
    auto& local_counts = *local[omp_get_thread_num()];
    auto [begin, end] = chunks[chunk];
    for (std::size_t i = begin; i < end; ++i) {
      const char c = input[i];
      if (static_cast<unsigned char>(c) <= ' ') {
        local_counts.count_one();
      } else local_counts.push(to_lower(c));
    }
    local_counts.count_one();
  }

  for (auto& local_counts : local) counts.merge(*local_counts);
  counts.dump(std::cout);
}
