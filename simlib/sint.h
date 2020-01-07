#ifndef SINT_H_
#define SINT_H_

#include <cinttypes>

#include "uint.h"


template<int w_>
class SInt {
  private: // copied from uint.h
  constexpr static int cmin(int wa, int wb) { return wa < wb ? wa : wb; }
  constexpr static int cmax(int wa, int wb) { return wa > wb ? wa : wb; }

public:
  SInt() : ui(0) {}

  SInt(int64_t i) : ui(i) {
    if (w_ > kWordSize)
      sign_extend(kWordSize - 1);
    else
      sign_extend();
  }

  SInt(std::string initial) : ui(initial) {
    sign_extend();
  }

  // TODO: make array's template parameters somehow inherit from ui
  SInt(std::array<uint64_t, (w_+63)/64> raw_input_reversed) : ui(raw_input_reversed) {
    sign_extend();
  }

  template<int other_w>
  explicit SInt(const SInt<other_w> &other) {
    static_assert(other_w <= w_, "Can't copy construct from wider SInt");
    ui = UInt<w_>(other.ui);
    sign_extend(other_w - 1);
  }

  SInt(const UInt<w_> &other) : ui(other) {}

  void rand_init() {
    ui.core_rand_init();
    sign_extend();
  }

  template<int out_w>
  SInt<cmax(w_,out_w)> pad() const {
    return SInt<cmax(w_,out_w)>(*this);
  }

  template<int other_w>
  SInt<w_ + other_w> cat(const SInt<other_w> &other) const {
    UInt<other_w> other_ui = other.ui;
    other_ui.mask_top_unused();
    SInt<w_ + other_w> result(ui.cat(other_ui));
    result.sign_extend();
    return result;
  }

  SInt<w_ + 1> operator+(const SInt<w_> &other) const {
    SInt<w_+1> result(ui.template core_add_sub<w_+1, false>(other.ui));
    if (w_ % kWordSize == 0) {
      if (negative() == other.negative()) {
        result.ui.words_[ui.word_index(w_)] = negative() ? -1 : 0;
      } else {
        bool is_neg = static_cast<int64_t>(result.ui.words_[ui.word_index(w_-1)]) < 0;
        result.ui.words_[ui.word_index(w_)] = is_neg ? -1 : 0;
      }
    }
    return result;
  }

  SInt<w_ + 1> operator+(const UInt<w_> &other) const {
    return pad<w_+1>().addw(SInt<w_+1>(other.template pad<w_+1>()));
  }

  SInt<w_> addw(const SInt<w_> &other) const {
    return ui.template core_add_sub<w_, false>(other.ui);
  }

  SInt<w_> subw(const SInt<w_> &other) const {
    SInt<w_> result(ui.template core_add_sub<w_, true>(other.ui));
    result.sign_extend();
    return result;
  }

  SInt<w_ + 1> operator-() const {
    return SInt<w_>(0) - *this;
  }

  SInt<w_ + 1> operator-(const SInt<w_> &other) const {
    SInt<w_ + 1> result(ui.template core_add_sub<w_+1, true>(other.ui));
    if (w_ % kWordSize == 0) {
      if (negative() != other.negative()) {
        result.ui.words_[ui.word_index(w_)] = negative() ? -1 : 0;
      } else {
        bool is_neg = static_cast<int64_t>(result.ui.words_[ui.word_index(w_-1)]) < 0;
        result.ui.words_[ui.word_index(w_)] = is_neg ? -1 : 0;
      }
    }
    return result;
  }

  SInt<w_ + 1> operator-(const UInt<w_> &other) const {
    return pad<w_+1>().subw(SInt<w_+1>(other.template pad<w_+1>()));
  }

  SInt<w_ + w_> operator*(const SInt<w_> &other) const {
    SInt<4*w_> product(pad<w_ + w_>().ui * other.pad<w_ + w_>().ui);
    SInt<w_ + w_> result = (product.template tail<w_ + w_>()).asSInt();
    result.sign_extend();
    return result;
  }

  SInt<w_ + w_> operator*(const UInt<w_> &other) const {
    SInt<w_ + w_ + 2> product(pad<w_+1>() * SInt<w_+1>(other.template pad<w_+1>()));
    SInt<w_ + w_> result = (product.template tail<2>()).asSInt();
    result.sign_extend();
    return result;
  }

  template<int other_w>
  SInt<w_+1> operator/(const SInt<other_w> &other) const {
    static_assert(w_ <= kWordSize, "Div not supported beyond 64b");
    static_assert(other_w <= kWordSize, "Div not supported beyond 64b");
    return SInt<w_+1>(as_single_word() / other.as_single_word());
  }

  template<int other_w>
  SInt<w_> operator/(const UInt<other_w> &other) const {
    return (pad<w_+1>() / SInt<w_+1>(other.template pad<w_+1>())).template tail<3>();
  }

  template<int other_w>
  SInt<cmin(w_, other_w)> operator%(const SInt<other_w> &other) const {
    static_assert(w_ <= kWordSize, "Mod not supported beyond 64b");
    static_assert(other_w <= kWordSize, "Mod not supported beyond 64b");
    return SInt<cmin(w_, other_w)>(as_single_word() % other.as_single_word());
  }

  template<int other_w>
  SInt<cmin(w_, other_w+1)> operator%(const UInt<other_w> &other) const {
    return (*this) % SInt<other_w+1>(other.template pad<w_+1>());
  }

  UInt<w_> operator~() const {
    return ~ui;
  }

  UInt<w_> operator&(const SInt<w_> &other) const {
    UInt<w_> result = ui & other.ui;
    result.mask_top_unused();
    return result;
  }

  UInt<w_> operator|(const SInt<w_> &other) const {
    UInt<w_> result = ui | other.ui;
    result.mask_top_unused();
    return result;
  }

  UInt<w_> operator^(const SInt<w_> &other) const {
    UInt<w_> result = ui ^ other.ui;
    result.mask_top_unused();
    return result;
  }

  UInt<1> andr() const {
    UInt<w_> upper_bits_clear = ui;
    upper_bits_clear.mask_top_unused();
    return upper_bits_clear.andr();
  }

  UInt<1> orr() const {
    UInt<w_> upper_bits_clear = ui;
    upper_bits_clear.mask_top_unused();
    return upper_bits_clear.orr();
  }

  UInt<1> xorr() const {
    UInt<w_> upper_bits_clear = ui;
    upper_bits_clear.mask_top_unused();
    return upper_bits_clear.xorr();
  }

  template<int hi, int lo>
  UInt<hi - lo + 1> bits() const {
    return ui.template bits<hi,lo>();
  }

  template<int n>
  UInt<n> head() const {
    static_assert(n <= w_, "Head n must be <= width");
    return bits<w_-1, w_-n>();
  }

  template<int n>
  UInt<w_ - n> tail() const {
    static_assert(n < w_, "Tail n must be < width");
    return bits<w_-n-1, 0>();
  }

  template<int shamt>
  SInt<w_ + shamt> shl() const {
    return cat(SInt<shamt>(0));
  }

  template<int shamt>
  SInt<w_> shlw() const {
    SInt<w_> result(ui.template shlw<shamt>());
    result.sign_extend();
    return result;
  }

  template<int shamt>
  SInt<w_ - shamt> shr() const {
    SInt<w_ - shamt> result(ui.template core_bits<w_-1, shamt>());
    result.sign_extend(w_ - shamt - 1);
    return result;
  }

  template<int other_w>
  SInt<w_> operator>>(const UInt<other_w> &other) const {
    uint64_t dshamt = other.as_single_word();
    SInt<w_> result(ui >> other);
    result.sign_extend(w_ - dshamt - 1);
    return result;
  }

  template<int other_w>
  SInt<w_ + (1<<other_w) - 1> operator<<(const UInt<other_w> &other) const {
    uint64_t dshamt = other.as_single_word();
    SInt<w_ + (1<<other_w) - 1> result(ui << other);
    result.sign_extend(w_ + dshamt - 1);
    return result;
  }

  template<int other_w>
  SInt<w_> dshlw(const UInt<other_w> &other) const {
    SInt<w_> result(ui.dshlw(other));
    result.sign_extend();
    return result;
  }

  UInt<1> operator<=(const SInt<w_> &other) const {
    // if (ui.NW == 1)
    //   return as_single_word() <= other.as_single_word();
    if (negative()) {
      if (other.negative())
        return ui >= other.ui;
      else
        return UInt<1>(1);
    } else {
      if (other.negative())
        return UInt<1>(0);
      else
        return ui <= other.ui;
    }
  }

  UInt<1> operator>=(const SInt<w_> &other) const {
    // if (ui.NW == 1)
    //   return as_single_word() >= other.as_single_word();
    if (negative()) {
      if (other.negative())
        return ui <= other.ui;
      else
        return UInt<1>(0);
    } else {
      if (other.negative())
        return UInt<1>(1);
      else
        return ui >= other.ui;
    }
  }

  UInt<1> operator<(const SInt<w_> &other) const {
    return ~(*this >= other);
  }

  UInt<1> operator>(const SInt<w_> &other) const {
    return ~(*this <= other);
  }

  UInt<1> operator==(const SInt<w_> &other) const {
    return ui == other.ui;
  }

  UInt<1> operator!=(const SInt<w_> &other) const {
    return ~(*this == other);
  }

  UInt<w_> asUInt() const {
    UInt<w_> result(ui);
    result.mask_top_unused();
    return result;
  }

  SInt<w_> asSInt() const {
    return SInt<w_>(*this);
  }

  SInt<w_> cvt() const {
    return SInt<w_>(*this);
  }

  // Direct access for ops that only need small signals
  int64_t as_single_word() const {
    static_assert(w_ <= kWordSize, "SInt too big for single int64_t");
    return ui.words_[0];
  }


protected:
  template<int other_w>
  friend class sint_wrapper_t;

  void raw_copy_in(uint64_t *src) {
    ui.raw_copy_in(src);
  }

  void raw_copy_out(uint64_t *dst) {
    ui.raw_copy_out(dst);
  }


private:
  UInt<w_> ui;

  const static int kWordSize = UInt<w_>::kWordSize;

  bool negative() const {
    return static_cast<int64_t>(ui.words_[ui.word_index(w_ - 1)]) < 0;
    // return (ui.words_[ui.word_index(w_ - 1)] >> ((w_-1) % kWordSize)) & 1;
  }

  void sign_extend(int sign_index = (w_-1)) {
    int sign_offset = sign_index % kWordSize;
    int sign_word = ui.word_index(sign_index);
    bool is_neg = (ui.words_[sign_word] >> sign_offset) & 1;
    ui.words_[sign_word] = (static_cast<int64_t>(ui.words_[sign_word]) <<
                             (kWordSize - sign_offset - 1)) >>
                             (kWordSize - sign_offset - 1);
    for (int i = sign_word+1; i < ui.NW; i++) {
      ui.words_[i] = is_neg ? -1 : 0;
    }
  }

  void print_to_stream(std::ostream& os) const {
    ui.print_to_stream(os);
  }

  template<int other_w, typename other_word_t, int other_n>
  friend class UInt;

  template<int w>
  friend class SInt;

  template<int w>
  friend std::ostream& operator<<(std::ostream& os, const SInt<w>& ui);
};

template<int w>
std::ostream& operator<<(std::ostream& os, const SInt<w>& si) {
  // static_assert(w <= SInt<w>::kWordSize, "SInt too big to print");
  // os << si.as_single_word() << "<S" << w << ">";
  // return os;
  si.print_to_stream(os);
  os << "<S" << w << ">";
  return os;
}

#endif  // SINT_H_
