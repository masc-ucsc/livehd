// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace livehd {

// An immutable name held once per process: a Name is one pointer, a copy copies the pointer, and two
// names are the same text exactly when the pointers are equal.
//
// It stands where a std::string was used as an identity that is copied far more often than it is
// built: the instance name of a placed cell is repeated by each of its connections, in each model of
// the design, and a hierarchical name is about a hundred characters long.
//
// It reads like a const std::string. It converts to `const std::string&` and to std::string_view,
// compares and orders by text (so a std::map keyed by Name iterates in the same order as one keyed by
// std::string), hashes by text, formats and concatenates. It cannot be edited in place: build the new
// text and assign it.
//
// The text is never released (a name table, not a cache). Creating a Name from text takes a lock;
// everything else is lock free and safe from any thread.
class Name {
public:
  Name() noexcept : s_(&empty_text()) {}
  Name(std::string_view text) : s_(intern(text)) {}    // NOLINT(google-explicit-constructor)
  Name(const std::string& text) : s_(intern(text)) {}  // NOLINT(google-explicit-constructor)
  Name(const char* text) : s_(intern(text == nullptr ? std::string_view{} : std::string_view{text})) {}  // NOLINT

  [[nodiscard]] const std::string& str() const noexcept { return *s_; }
  [[nodiscard]] std::string_view   view() const noexcept { return *s_; }
  operator const std::string&() const noexcept { return *s_; }  // NOLINT(google-explicit-constructor)
  operator std::string_view() const noexcept { return *s_; }    // NOLINT(google-explicit-constructor)
  const std::string* operator->() const noexcept { return s_; }
  const std::string& operator*() const noexcept { return *s_; }

  // The read-only part of std::string that callers use directly.
  [[nodiscard]] bool        empty() const noexcept { return s_->empty(); }
  [[nodiscard]] size_t      size() const noexcept { return s_->size(); }
  [[nodiscard]] size_t      length() const noexcept { return s_->size(); }
  [[nodiscard]] const char* c_str() const noexcept { return s_->c_str(); }
  [[nodiscard]] const char* data() const noexcept { return s_->data(); }
  [[nodiscard]] auto        begin() const noexcept { return s_->begin(); }
  [[nodiscard]] auto        end() const noexcept { return s_->end(); }
  [[nodiscard]] char        front() const { return s_->front(); }
  [[nodiscard]] char        back() const { return s_->back(); }
  [[nodiscard]] char        operator[](size_t i) const { return (*s_)[i]; }
  template <class... A>
  [[nodiscard]] bool starts_with(A&&... a) const {
    return s_->starts_with(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] bool ends_with(A&&... a) const {
    return s_->ends_with(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] bool contains(A&&... a) const {
    return s_->contains(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] size_t find(A&&... a) const {
    return s_->find(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] size_t rfind(A&&... a) const {
    return s_->rfind(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] size_t find_first_of(A&&... a) const {
    return s_->find_first_of(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] size_t find_last_of(A&&... a) const {
    return s_->find_last_of(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] size_t find_first_not_of(A&&... a) const {
    return s_->find_first_not_of(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] size_t find_last_not_of(A&&... a) const {
    return s_->find_last_not_of(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] std::string substr(A&&... a) const {
    return s_->substr(std::forward<A>(a)...);
  }
  template <class... A>
  [[nodiscard]] int compare(A&&... a) const {
    return s_->compare(std::forward<A>(a)...);
  }

  // Comparison and concatenation are hidden friends: they are found only when an operand IS a Name, so text
  // compared with text never converts to one.
  friend bool                 operator==(const Name& a, const Name& b) noexcept { return a.s_ == b.s_; }
  friend std::strong_ordering operator<=>(const Name& a, const Name& b) noexcept {
    return a.s_ == b.s_ ? std::strong_ordering::equal : order(*a.s_, *b.s_);
  }
  // Against text (a literal, a std::string, a std::string_view) the comparison is by content and builds no Name.
  template <class T>
    requires(std::convertible_to<const T&, std::string_view> && !std::same_as<std::remove_cvref_t<T>, Name>)
  friend bool operator==(const Name& a, const T& b) noexcept {
    return a.view() == std::string_view(b);
  }
  template <class T>
    requires(std::convertible_to<const T&, std::string_view> && !std::same_as<std::remove_cvref_t<T>, Name>)
  friend std::strong_ordering operator<=>(const Name& a, const T& b) noexcept {
    return order(a.view(), std::string_view(b));
  }

  friend std::string operator+(const Name& a, const Name& b) { return a.str() + b.str(); }
  friend std::string operator+(const Name& a, char b) { return a.str() + b; }
  friend std::string operator+(char a, const Name& b) { return a + b.str(); }
  template <class T>
    requires(std::convertible_to<const T&, std::string_view> && !std::same_as<std::remove_cvref_t<T>, Name>)
  friend std::string operator+(const Name& a, const T& b) {
    std::string out(a.str());
    out.append(std::string_view(b));
    return out;
  }
  template <class T>
    requires(std::convertible_to<const T&, std::string_view> && !std::same_as<std::remove_cvref_t<T>, Name>)
  friend std::string operator+(const T& a, const Name& b) {
    std::string out{std::string_view(a)};
    out.append(b.str());
    return out;
  }
  friend std::string operator+(std::string&& a, const Name& b) {
    a.append(b.str());
    return std::move(a);
  }

  friend std::ostream&    operator<<(std::ostream& os, const Name& name) { return os << name.str(); }
  // fmt finds this by argument-dependent lookup.
  friend std::string_view format_as(const Name& name) noexcept { return name.view(); }

private:
  friend class Pin_label;
  // A name that is equal to no text: Pin_label marks a label with no instance part with it.
  explicit Name(const std::string* marker) noexcept : s_(marker) {}
  static std::strong_ordering order(std::string_view a, std::string_view b) noexcept {
    const int c = a.compare(b);
    return c < 0 ? std::strong_ordering::less : c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal;
  }
  static const std::string* intern(std::string_view text);
  static const std::string& empty_text() noexcept;

  const std::string* s_;
};

// The identity of a pin for timing, RC and demand: the text "instance:pin", or the bare name of a top-level
// terminal. It holds the two parts as Names (two pointers) and never the joined text, which for a
// hierarchical instance is over a hundred characters and is different for every pin of the design.
//
// It reads like the text: it is built from it and converts to it, and it compares and orders exactly as
// the joined text does, so a std::map keyed by Pin_label iterates in the same order as one keyed by the
// std::string label.
class Pin_label {
public:
  Pin_label() noexcept : instance_(bare_marker()) {}
  Pin_label(std::string_view text) { assign(text); }                                                      // NOLINT
  Pin_label(const std::string& text) { assign(text); }                                                    // NOLINT
  Pin_label(const char* text) { assign(text == nullptr ? std::string_view{} : std::string_view{text}); }  // NOLINT
  Pin_label(const Name& text) { assign(text.view()); }                                                    // NOLINT
  // The label of a connection: `instance` "PIN" names a top-level terminal, whose label is the bare pin name.
  Pin_label(const Name& instance, const Name& pin) {
    if (instance == "PIN") {
      assign(pin.view());
    } else if (pin.contains(':')) {
      assign(instance + ":" + pin);
    } else {
      instance_ = instance;
      pin_      = pin;
    }
  }

  [[nodiscard]] bool        empty() const noexcept { return bare() && pin_.empty(); }
  // A top-level terminal (or any text without ':').
  [[nodiscard]] bool        bare() const noexcept { return instance_ == bare_marker(); }
  // The part before the last ':' (empty for a bare label) and the part after it (the whole text for a bare label).
  [[nodiscard]] Name        instance() const noexcept { return bare() ? Name() : instance_; }
  [[nodiscard]] const Name& pin() const noexcept { return pin_; }
  [[nodiscard]] size_t      size() const noexcept { return bare() ? pin_.size() : instance_.size() + 1 + pin_.size(); }
  [[nodiscard]] std::string str() const {
    if (bare()) {
      return pin_.str();
    }
    std::string out;
    out.reserve(instance_.size() + 1 + pin_.size());
    out.append(instance_.str()).append(1, ':').append(pin_.str());
    return out;
  }
  operator std::string() const { return str(); }  // NOLINT(google-explicit-constructor)
  // What callers that split the text by hand ask for. The last ':' of the text is the one between the parts.
  [[nodiscard]] size_t rfind(char c) const noexcept {
    if (!bare() && c == ':') {
      return instance_.size();
    }
    if (const auto at = pin_.rfind(c); at != std::string::npos) {
      return bare() ? at : instance_.size() + 1 + at;
    }
    return bare() ? std::string::npos : instance_.rfind(c);
  }
  [[nodiscard]] size_t find(char c) const noexcept {
    if (bare()) {
      return pin_.find(c);
    }
    if (const auto at = instance_.find(c); at != std::string::npos) {
      return at;
    }
    if (c == ':') {
      return instance_.size();
    }
    const auto at = pin_.find(c);
    return at == std::string::npos ? at : instance_.size() + 1 + at;
  }
  [[nodiscard]] bool        starts_with(std::string_view prefix) const { return str().starts_with(prefix); }
  [[nodiscard]] std::string substr(size_t pos = 0, size_t count = std::string::npos) const {
    if (!bare() && pos == 0 && count == instance_.size()) {
      return instance_.str();
    }
    if (!bare() && pos == instance_.size() + 1 && count >= pin_.size()) {
      return pin_.str();
    }
    return str().substr(pos, count);
  }

  friend bool operator==(const Pin_label& a, const Pin_label& b) noexcept { return a.instance_ == b.instance_ && a.pin_ == b.pin_; }
  friend std::strong_ordering operator<=>(const Pin_label& a, const Pin_label& b) noexcept {
    if (a.instance_ == b.instance_) {
      return a.pin_ <=> b.pin_;  // the same prefix (or two bare labels)
    }
    return order(a.parts(), b.parts());
  }
  // Against text the comparison is by content and builds neither a label nor a string.
  template <class T>
    requires(std::convertible_to<const T&, std::string_view> && !std::same_as<std::remove_cvref_t<T>, Name>)
  friend bool operator==(const Pin_label& a, const T& b) noexcept {
    return order(a.parts(), Parts{std::string_view(b), {}, {}}) == std::strong_ordering::equal;
  }
  template <class T>
    requires(std::convertible_to<const T&, std::string_view> && !std::same_as<std::remove_cvref_t<T>, Name>)
  friend std::strong_ordering operator<=>(const Pin_label& a, const T& b) noexcept {
    return order(a.parts(), Parts{std::string_view(b), {}, {}});
  }
  template <class T>
    requires(std::convertible_to<const T&, std::string_view>)
  friend std::string operator+(const Pin_label& a, const T& b) {
    return a.str().append(std::string_view(b));
  }
  template <class T>
    requires(std::convertible_to<const T&, std::string_view>)
  friend std::string operator+(const T& a, const Pin_label& b) {
    return std::string(std::string_view(a)).append(b.str());
  }
  // For lookup tables only: it mixes the addresses of the two parts, so the iteration order of an unordered
  // container keyed by a label follows neither the text nor the run.
  [[nodiscard]] size_t hash() const noexcept {
    const auto a = std::hash<const void*>{}(&instance_.str()), b = std::hash<const void*>{}(&pin_.str());
    return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
  }
  friend std::ostream& operator<<(std::ostream& os, const Pin_label& label) { return os << label.str(); }
  friend std::string   format_as(const Pin_label& label) { return label.str(); }

private:
  // The text as up to three pieces, compared as if joined.
  struct Parts {
    std::string_view piece[3];
  };
  [[nodiscard]] Parts parts() const noexcept {
    return bare() ? Parts{pin_.view(), {}, {}} : Parts{instance_.view(), std::string_view(":"), pin_.view()};
  }
  static std::strong_ordering order(const Parts& a, const Parts& b) noexcept {
    size_t           ia = 0, ib = 0;
    std::string_view sa = a.piece[0], sb = b.piece[0];
    for (;;) {
      while (sa.empty() && ia < 2) {
        sa = a.piece[++ia];
      }
      while (sb.empty() && ib < 2) {
        sb = b.piece[++ib];
      }
      if (sa.empty() || sb.empty()) {
        return sa.empty() && sb.empty() ? std::strong_ordering::equal
               : sa.empty()             ? std::strong_ordering::less
                                        : std::strong_ordering::greater;
      }
      const size_t n = std::min(sa.size(), sb.size());
      if (const int c = std::char_traits<char>::compare(sa.data(), sb.data(), n); c != 0) {
        return c < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
      }
      sa.remove_prefix(n);
      sb.remove_prefix(n);
    }
  }
  void assign(std::string_view text) {
    if (const auto colon = text.rfind(':'); colon == std::string_view::npos) {
      instance_ = bare_marker();
      pin_      = Name(text);
    } else {
      instance_ = Name(text.substr(0, colon));
      pin_      = Name(text.substr(colon + 1));
    }
  }
  static Name bare_marker() noexcept;

  Name instance_, pin_;
};

}  // namespace livehd

template <>
struct std::formatter<livehd::Pin_label> : std::formatter<std::string> {
  auto format(const livehd::Pin_label& label, auto& ctx) const { return std::formatter<std::string>::format(label.str(), ctx); }
};
template <>
struct std::hash<livehd::Pin_label> {
  size_t operator()(const livehd::Pin_label& label) const noexcept { return label.hash(); }
};
template <template <class> class TQ, template <class> class UQ>
struct std::basic_common_reference<livehd::Pin_label, std::string, TQ, UQ> {
  using type = std::string;
};
template <template <class> class TQ, template <class> class UQ>
struct std::basic_common_reference<std::string, livehd::Pin_label, TQ, UQ> {
  using type = std::string;
};

// A Name and a std::string meet as text: this is what lets the constrained algorithms
// (std::ranges::find(nets, text, &Net::name)) compare one with the other.
template <template <class> class TQ, template <class> class UQ>
struct std::basic_common_reference<livehd::Name, std::string, TQ, UQ> {
  using type = std::string_view;
};
template <template <class> class TQ, template <class> class UQ>
struct std::basic_common_reference<std::string, livehd::Name, TQ, UQ> {
  using type = std::string_view;
};

template <>
struct std::hash<livehd::Name> {
  size_t operator()(const livehd::Name& name) const noexcept { return std::hash<std::string_view>{}(name.view()); }
};

template <>
struct std::formatter<livehd::Name> : std::formatter<std::string_view> {
  auto format(const livehd::Name& name, auto& ctx) const { return std::formatter<std::string_view>::format(name.view(), ctx); }
};
