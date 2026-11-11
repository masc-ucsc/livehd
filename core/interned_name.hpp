// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

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
  static std::strong_ordering order(std::string_view a, std::string_view b) noexcept {
    const int c = a.compare(b);
    return c < 0 ? std::strong_ordering::less : c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal;
  }
  static const std::string* intern(std::string_view text);
  static const std::string& empty_text() noexcept;

  const std::string* s_;
};

}  // namespace livehd

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
