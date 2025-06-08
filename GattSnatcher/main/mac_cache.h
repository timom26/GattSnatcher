#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <unordered_map>

struct MacKey {
  uint8_t addr[6];
  uint8_t addr_type;
  bool operator==(MacKey const& o) const;
};

struct MacKeyHash {
  std::size_t operator()(MacKey const& k) const;
};

struct Node {
  MacKey   key;
  int64_t  timestamp;
  Node*    prev;
  Node*    next;
};

class MacCache {
public:
  static constexpr int64_t TTL = 20LL * 60 * 1000000; // 20 minutes in μs

  MacCache();
  ~MacCache();

  // Returns true if the entry is new or expired and should be printed
  bool shouldPrintAndAddToCache(MacKey const& k, int64_t now);

  // Evict entries older than TTL
  void evictOld(int64_t now);

private:
  void appendToTail(Node* n);
  void moveToTail(Node* n);

  std::unordered_map<MacKey, Node*, MacKeyHash> map_;
  Node* head_ = nullptr;
  Node* tail_ = nullptr;
};
