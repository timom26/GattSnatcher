#include "mac_cache.h"
#include <cstring>

// MacKey definitions
bool MacKey::operator==(MacKey const& o) const {
  return addr_type == o.addr_type
      && std::memcmp(addr, o.addr, 6) == 0;
}

std::size_t MacKeyHash::operator()(MacKey const& k) const {
  // FNV-1a hash over the 6-byte address, mixed with addr_type
  std::size_t h = 146527;
  for (int i = 0; i < 6; ++i) {
    h ^= k.addr[i];
    h *= 1099511628211ULL;
  }
  return h ^ (std::size_t)k.addr_type;
}

// MacCache constructor/destructor
MacCache::MacCache() = default;

MacCache::~MacCache() {
  Node* curr = head_;
  while (curr) {
    Node* next = curr->next;
    delete curr;
    curr = next;
  }
}

//will add too
bool MacCache::shouldPrintAndAddToCache(MacKey const& k, int64_t now) {
  auto it = map_.find(k);
  if (it != map_.end()) {
    Node* n = it->second;
    if (now - n->timestamp < TTL) {
      return false;
    }
    n->timestamp = now;
    moveToTail(n);
    return true;
  }
  Node* n = new Node{k, now, nullptr, nullptr};
  map_[k] = n;
  appendToTail(n);
  return true;
}

void MacCache::evictOld(int64_t now) {
  while (head_ && now - head_->timestamp > TTL) {
    Node* old = head_;
    map_.erase(old->key);
    head_ = old->next;
    if (head_) head_->prev = nullptr;
    else       tail_  = nullptr;
    delete old;
  }
}

void MacCache::appendToTail(Node* n) {
  if (!tail_) {
    head_ = tail_ = n;
  } else {
    tail_->next = n;
    n->prev     = tail_;
    tail_       = n;
  }
}

void MacCache::moveToTail(Node* n) {
  if (n == tail_) return;
  if (n->prev) n->prev->next = n->next;
  else         head_         = n->next;
  if (n->next) n->next->prev = n->prev;
  n->prev      = tail_;
  n->next      = nullptr;
  tail_->next  = n;
  tail_        = n;
}
