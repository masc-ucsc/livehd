const assert = require('assert');
assert(50 < 70, 'this is not true');
// CITATION
// https://tc39.es/proposal-bigint/#sec-exp-operator
// https://stackoverflow.com/questions/54758130/how-to-obtain-the-amount-of-bits-of-a-bigint

// 🐅🐅🐅testing workspace🐀🐀🐀

// helper function
function BigIntnumberConversion(target = '0', from_base = 10, to_base = 10) {
  const numberString = parseInt(target, from_base).toString(to_base);
  /* console.log(target, "numberString", numberString) */
  return BigInt(numberString);
}

function isDigit(str) {
  return /^\d+$/.test(str);
}

// console.log('answer', BigIntnumberConversion('1', -1));

class Lconst {
  constructor(
    number,
    explicit_str = false,
    bits = 0n,
    num = 0n,
    has_unknown = false
  ) {
    this.number = number;
    this.explicit_str = explicit_str;
    this.bits = bits;
    this.num = num;
    this.unknown = 0n;
    this.has_unknown = has_unknown;
    this.initialized();
  }

  initialized() {
    if (this.number) {
      if (typeof this.number === 'string') {
        this.explicit_str = false;
        this.bits = 0n;
        this.num = 0n;
      } else {
        //question: how to deal with the size of number is int64_t or Number(bigInt)
        this.explicit_str = false;
        this.num = BigInt(this.number);
        this.bits = Lconst.calc_num_bits(this.num);
      }
    }
  }
  // ======== support functions ======================
  static calc_num_bits(number) {
    // count implicit sign bits
    const bigI = number > 0 ? BigInt(number) : -1n * BigInt(number);
    const binaryForm = bigI.toString(2);
    return BigInt(binaryForm.length + 1);
  }

  static new_lconst(explicit_str, bits, num, has_unknown = false, unknown) {
    const new_l = new Lconst();
    new_l.explicit_str = explicit_str;
    new_l.bits = BigInt(bits);
    new_l.num = BigInt(num);
    new_l.has_unknown = has_unknown;
    if (new_l.has_unknown) new_l.unknown = unknown;
    return new_l;
  }

  // not sure why bits is less than 62
  is_i() {
    return !this.explicit_str && this.bits <= 62;
  }

  to_i() {
    // return static_cast<long int>(num);
    return this.num;
  }

  // ======= new Lconst returned =======================
  static from_pyrope(number_str) {
    // check, the input must be a string
    if (typeof number_str != 'string') {
      throw new TypeError('the input must be a string');
    }

    if (number_str.length === 0) return new Lconst();

    let txt = number_str.toLowerCase();
    txt = txt.replace(/_/g, '');

    // special cases !!!
    if (txt === 'true') {
      return Lconst.new_lconst(false, 1n, -1n);
    } else if (txt === 'false') {
      return Lconst.new_lconst(false, 1n, 0n);
    }

    let skip_chars = 0;
    let shift_mode = -1;
    var negative = false; // does it have negative sign?
    let unsigned_result = false; // true if start with 0sb

    if (txt[0] === '-') {
      negative = true;
      skip_chars = 1;
    } else if (txt[0] === '+') {
      skip_chars = 1;
    }

    if (txt.length >= skip_chars + 1 && isDigit(txt[skip_chars])) {
      shift_mode = 10;
      if (txt.length >= 2 + skip_chars && txt[skip_chars] === '0') {
        skip_chars += 1;
        let sel_ch = txt[skip_chars];

        if (sel_ch === 's') {
          skip_chars += 1;
          sel_ch = txt[skip_chars];
          if (sel_ch !== 'b') {
            throw new Error(
              `ERROR: ${number_str} unknown pyrope encoding only binary can be signed 0sb...`
            );
          }
          assert(
            !unsigned_result,
            `ERROR: ${number_str} have unsigned_result = FALSE...`
          );
        } else {
          unsigned_result = true;
        }

        if (sel_ch === 'x') {
          shift_mode = 16;
          skip_chars += 1;
        } else if (sel_ch === 'b') {
          shift_mode = 2;
          skip_chars += 1;
        } else if (sel_ch === 'd') {
          shift_mode = 10;
          skip_chars += 1;
        } else if (isDigit(sel_ch)) {
          shift_mode = 8;
        } else if (sel_ch === 'o') {
          shift_mode = 8;
          skip_chars += 1;
        } else {
          throw new Error(
            `ERROR: ${number_str} unknown pyrope encoding (leading ${sel_ch})...`
          );
        }
      }
    } else {
      let start_i = number_str.length;
      let end_i = 0;

      if (
        number_str.length > 1 &&
        number_str.charAt(0) === "'" &&
        number_str.charAt(start_i) === "'"
      ) {
        --start_i;
        ++end_i;
      }

      let bigNumber = BigInt(0);
      let prev_escaped = false;
      for (let i = start_i - 1; i >= end_i; --i) {
        bigNumber <<= 8n;
        bigNumber |= BigInt(number_str.charCodeAt(i));

        if (number_str[i] === "'" && !prev_escaped && i !== 0) {
          throw new Error(
            `ERROR: ${number_str} malformed pyrope string. ' must be escaped`
          );
        }

        if (number_str[i] === '\\') {
          if (prev_escaped) prev_escaped = false;
          else prev_escaped = true;
        }
        return Lconst.new_lconst(true, (start_i - end_i) * 8, bigNumber);
      }
    }

    let num = BigInt(0);
    let to_power = -1n;

    if (shift_mode === 10) {
      for (let i = skip_chars; i < txt.length; i++) {
        if (txt[i] >= 0) {
          num = 10n * num + BigInt(txt[i]);
        } else {
          throw new Error(
            `ERROR: ${number_str} encoding could not use ${txt[i]}`
          );
        }
      }
    } else if (shift_mode === 2) {
      //待测试
      let v = Lconst.from_binary(txt.substring(skip_chars), unsigned_result);
      if (!negative) return v;
      // Prob:
      v.num *= -1n;
      return v;
    } else {
      assert(
        shift_mode === 16 || shift_mode === 8,
        `ERROR: ${number_str} should be either hexa or octal...`
      );
      for (let i = txt.length - 1; i >= skip_chars; --i) {
        if (
          (shift_mode === 16 && !/^(\d|[abcdef])$/.test(txt[i])) |
          (shift_mode === 8 && !/^[0-7]$/.test(txt[i]))
        ) {
          throw new Error(
            `ERROR: ${number_str} encoding could not use ${txt[i]}`
          );
        }
        to_power += 1n;
        // console.log(i + ' current letter is ' + txt[i] + ' shift mode ' + shift_mode + ' to power ' + to_power);
        num +=
          BigIntnumberConversion(txt[i], shift_mode) *
          BigInt(shift_mode) ** to_power;
      }
    }

    if (negative) {
      num = -num;
      if (unsigned_result && num < 0n) {
        throw new Error(
          `ERROR, ${number_str} negative value but it must be unsigned`
        );
      }
    }

    return Lconst.new_lconst(false, Lconst.calc_num_bits(num), num);
  } // end of from_pyrope

  static from_binary(txt, unsigned_result) {
    assert(
      typeof unsigned_result === 'boolean',
      `ERROR: unsigned_result ${unsigned_result} is not Boolean type`
    );
    const ori_txt = txt.replace(/_/g, '');
    let num = 0n;
    let unknown = 0n;
    let unknown_found = false;
    let len_num = 0n;
    let negative = 0;

    // find the sign
    if (!unsigned_result) {
      if (ori_txt[0] === '1') {
        negative = 1;
      }
    }

    for (const char of ori_txt) {
      /* console.log('-----current char----', char);
      console.log('check the value of binary: ', num);
      console.log('check the value of unknown: ', unknown); */
      if (char === '?' || char === 'x' || char === 'z') {
        num <<= 1n;
        len_num += 1n;
        unknown = (unknown << 1n) | 1n;
        unknown_found = true;
      } else if (char === '0') {
        if (num !== 0n || unknown !== 0n) {
          /* console.log('get inside of the loop: ', num); */
          unknown <<= 1n;
          num <<= 1n;
          len_num += 1n;
          /* console.log('what1', num); */
        }
      } else if (char === '1') {
        if (num !== 1n || !negative) {
          /*  console.log('get inside of the loop: ', num); */
          num = (num << 1n) | 1n;
          len_num += 1n;
          unknown <<= 1n;
          /*  console.log('what2', num); */
        }
      } else {
        throw new Error(`ERROR: ${txt} binary encoding could not use ${char}`);
      }
    }

    if (!unsigned_result && negative) {
      // =====================================
      /* console.log('signed_result and num[0] is 1', num);
      console.log('len_num:', len_num); */
      num = num - (1n << len_num);
    }

    let return_Lconst = Lconst.new_lconst(
      unknown_found, //if there is unknown in the string, then it is an explicit string
      Lconst.calc_num_bits(num),
      num,
      unknown_found
    );
    return_Lconst.unknown = unknown;
    return return_Lconst;
  }

  static from_string(orig_txt) {
    let num = 0n;
    for (const char of orig_txt) {
      num <<= 8n;
      num += BigInt(char.charCodeAt(0));
    }

    return Lconst.new_lconst(true, BigInt(8 * orig_txt.length), num);
  }

  // restriction: only from decimal to pyrope
  to_pyrope() {
    let output = '0x';
    return output + this.num.toString(16);
  }

  to_string() {
    let str = '';
    let tmp = this.num;
    while (tmp) {
      let ch = tmp & 0xffn;
      str += ch;
      tmp >>= 8n;
    }
    return str;
  }

  // TESTING
  to_binary() {
    if (this.has_unknown) {
      return this.to_string();
    }

    let v = this.num;
    if (v === 0n) return '0';

    let txt = '';
    /* console.log(this.bits, this.num); */
    for (let i = 0n; i < this.bits; ++i) {
      if (v & 1n) {
        txt = '1' + txt;
      } else {
        txt = '0' + txt;
      }
      v = v >> 1n;
    }
    return txt;
  }

  adjust(com_lconst) {
    if (!(com_lconst instanceof Lconst)) {
      throw new Error(
        `ERROR the input ${com_lconst} must be an instance of Lconst`
      );
    }
    this.explicit_str =
      com_lconst.explicit_str && (this.bits === 0 || this.explicit_str);
    this.bits = Lconst.calc_num_bits(this.num);
  }
  is_negative() {
    // return true if its num is less than 0
    if (!this.explicit_str) return this.num < 0;

    // if it is a string and has unknown
    if (!this.has_unknown) return false;

    // if is a string and does not have unknown
    // ...
  }

  is_positive() {
    if (!this.explicit_str) return this.num >= 0;
    if (!this.has_unknown) return false;
  }

  // ========= operation =============
  // restriction: the num of these two objects do not have underscore and '?'
  and_op(com_lconst) {
    if (!(com_lconst instanceof Lconst)) {
      throw TypeError(
        `ERROR the input ${com_lconst} must be an instance of Lconst`
      );
    }

    let res = new Lconst();
    res.num = this.num & com_lconst.num;
    if (com_lconst.has_unknown || this.has_unknown) {
      // notice that has_unknown toggles into True from false only in function from_binary
      // PRIORITY OF three logic: 0 > ? > 1
      //first step: get an expression, where '1' represents 1 or ?, and '0' represents 0

      const zeroTo0 =
        (this.num | this.unknown) & (com_lconst.num | com_lconst.unknown);
      /* console.log(this.num, this.unknown, com_lconst.num, com_lconst.unknown); */
      res.unknown = (this.unknown | com_lconst.unknown) & zeroTo0;
      res.has_unknown = true;
      return res;
    }
    // case: neither of them has unknown
    res.adjust(com_lconst);
    return res;
  }

  or_op(com_lconst) {
    if (!(com_lconst instanceof Lconst)) {
      throw TypeError(
        `ERROR the input ${com_lconst} must be an instance of Lconst`
      );
    }

    let res = new Lconst();
    res.num = this.num | com_lconst.num;
    if (com_lconst.has_unknown || this.has_unknown) {
      // notice that has_unknown toggles into True from false only in function from_binary
      // PRIORITY OF three logic: 1 > ? > 0
      res.unknown = (this.unknown | com_lconst.unknown) & ~res.num;
      res.has_unknown = true;
      return res;
    }
    // case: neither of them has unknown
    res.adjust(com_lconst);
    return res;
  }

  add_op(com_lconst) {
    if (!(com_lconst instanceof Lconst)) {
      throw TypeError(
        `ERROR the input ${com_lconst} must be an instance of Lconst`
      );
    }

    let set_checker = 1n;

    if (this.has_unknown || com_lconst.has_unknown) {
      let num = 0n;
      let unknown = 0n;
      let carry = 0;

      while (true) {
        let n_ones = 0;
        let n_unknowns = 0;
        if (set_checker <= this.num && set_checker & this.num) {
          n_ones += 1;
        }

        if (set_checker <= com_lconst.num && set_checker & com_lconst.num) {
          n_ones += 1;
        }

        if (set_checker <= this.unknown && set_checker & this.unknown) {
          n_unknowns += 1;
        }

        if (
          set_checker <= com_lconst.unknown &&
          set_checker & com_lconst.unknown
        ) {
          n_unknowns += 1;
        }

        if (carry === 1) {
          n_ones += 1;
        } else if (carry === -1) {
          n_unknowns += 1;
        }

        if (n_unknowns === 0 && n_ones === 0) {
          carry = 0;
        } else if (n_unknowns === 0 && n_ones === 1) {
          num ^= set_checker;
          carry = 0;
        } else if (n_unknowns === 0 && n_ones === 2) {
          carry = 1;
        } else if (n_unknowns === 0 && n_ones === 3) {
          num ^= set_checker;
          carry = 1;
        } else if (n_unknowns === 1 && n_ones === 0) {
          unknown ^= set_checker;
          carry = 0;
        } else if (n_unknowns === 1 && n_ones === 2) {
          unknown ^= set_checker;
          carry = 1;
        } else {
          unknown ^= set_checker;
          carry = -1;
        }
        console.log(set_checker, num, unknown);

        set_checker <<= 1n;

        if (
          set_checker > this.num &&
          set_checker > this.unknown &&
          set_checker > com_lconst.num &&
          set_checker > com_lconst.unknown &&
          carry === 0
        )
          break;
      }

      return Lconst.new_lconst(
        false,
        Lconst.calc_num_bits(num),
        num,
        true,
        unknown
      );
    }

    let res = new Lconst();
    res.num = this.num + com_lconst.num;
    res.adjust(com_lconst);
    return res;
  }

  is_string() {
    return this.explicit_str && !this.has_unknown;
  }

  sub_op(com_lconst) {
    // in sub operation, we cannot have unknown values
    if (this.is_string() || com_lconst.is_string()) {
      throw TypeError(`ERROR: not allowed because one is string.`);
    }

    if (typeof com_lconst !== Lconst) {
      throw TypeError(
        `ERROR: not allowed because the take in value is not Lconst`
      );
    }

    let res = new Lconst();
    res.num = this.num - com_lconst.num;
    res.adjust(com_lconst);
    return res;
  }

  mult_op(com_lconst) {
    if (this.is_string() || com_lconst.is_string())
      throw TypeError(`ERROR: not allowed because one is string.`);

    if (!com_lconst instanceof Lconst)
      throw TypeError(
        `ERROR: not allowed because the take in value is not Lconst`
      );

    if (this.has_unknown || com_lconst.has_unknown) {
      let n1 = this.is_negative() ? -1 : 1;
      let n2 = com_lconst.is_negative() ? -1 : 1;
      if (n1 * n2 < 0)
        return Lconst.unknown_negative(this.bits + com_lconst.bits);
      console.log('it is positive');
      return Lconst.unknown_positive(this.bits + com_lconst.bits);
    }

    let res = new Lconst();
    res.num = this.num * com_lconst.num;
    res.adjust(com_lconst);
    return res;
  }

  div_op(com_lconst) {
    if (this.is_string() || com_lconst.is_string())
      throw TypeError(`ERROR: not allowed because one is string.`);

    if (!com_lconst instanceof Lconst)
      throw TypeError(
        `ERROR: not allowed because the take in value is not Lconst`
      );

    if (com_lconst.num === 0n) {
      if (this.is_negative()) return Lconst.unknown_negative(2n);
      return Lconst.unknown_positive(2n);
    }
    if (this.has_unknown || com_lconst.has_unknown) {
      let n1 = this.is_negative() ? -1 : 1;
      let n2 = com_lconst.is_negative() ? -1 : 1;

      let b = this.bits;
      if (!com_lconst.has_unknown) {
        b -= com_lconst.bits;
        if (b <= 0) return new Lconst(0);
      }

      if (n1 * n2 < 0) return Lconst.unknown_negative(b);
      return Lconst.unknown_positive(b);
    }

    let res = new Lconst();
    res.num = this.num / com_lconst.num;
    res.adjust(com_lconst);
    return res;
  }

  /*   concat_op(com_lconst) {
    if (this.is_string() || com_lconst.is_string()) {
      let str = '';
      let com_str = '';

      if (this.is_string()) str = this.to_string();
      else if (this.is_i()) str = String(this.to_i());
      else str = this.to_binary();

      if (com_lconst.is_string()) com_str = com_lconst.to_string();
      else if (com_lconst.is_i()) com_lconst = String(com_lconst.to_i);
      else com_lconst = this.to_binary();

      // question
      return Lconst.from_string();
    }

    let res_num = (this.num << com_lconst.bits) | com_lconst.num;

    return Lconst.new_lconst(false, Lconst.calc_num_bits(res_num), res_num);
  } */

  static unknown(nbits) {
    let res = new Lconst();
    for (let i = 0; i < nbits; i++) {
      res.num <<= 8;
      res.num |= '?'.charCodeAt(0);
    }
    res.bits = nbits;
    if (nbits > 0) res.explicit_str = true;
    return res;
  }

  static unknown_positive(nbits) {
    let res = new Lconst();
    let questionMark = BigInt('?'.charCodeAt(0));
    let zero = BigInt('0'.charCodeAt(0));
    for (let i = 0n; i < nbits - 1n; i++) {
      res.num <<= 8n;
      res.num |= questionMark;
      console.log(res.num);
    }
    res.bits = nbits;
    if (nbits > 1n) {
      res.num <<= 8n;
      console.log(res.num);
      res.num |= zero;
      res.explicit_str = true;
      console.log(res.num);
    }
    return res;
  }

  static unknown_negative(nbits) {
    let res = new Lconst();
    let questionMark = BigInt('?'.charCodeAt(0));
    let one = BigInt('1'.charCodeAt(0));
    for (let i = 0n; i < nbits - 1n; i++) {
      res.num <<= 8n;
      res.num |= questionMark;
    }
    res.bits = nbits;
    if (nbits > 1n) {
      res.num <<= 8n;
      res.num |= one;
      res.explicit_str = true;
    }

    return res;
  }

  xor_op(com_lconst) {
    const num = this.num ^ com_lconst.num;
    return Lconst.new_lconst(false, Lconst.calc_num_bits(num), num);
  }
} // end of the class ————————————————————————————————————————————

// 🐕testing workspace for Lconst🐇
const testing1 = Lconst.from_pyrope('?');
console.log(testing1);
module.exports = Lconst;
