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
    bits = 0,
    num = 0,
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
        this.bits = 0;
        this.num = 0;
      } else {
        //question: how to deal with the size of number is int64_t or Number(bigInt)
        this.explicit_str = false;
        this.num = this.number;
        this.bits = Lconst.calc_num_bits(this.num);
      }
    }
  }
  // ======== support functions ======================
  static calc_num_bits(number) {
    // count implicit sign bits
    const bigI = number > 0 ? BigInt(number) : -1n * BigInt(number);
    const binaryForm = bigI.toString(2);
    return binaryForm.length + 1;
  }

  static new_lconst(explicit_str, bits, num, has_unknown = false) {
    const new_l = new Lconst();
    new_l.explicit_str = explicit_str;
    new_l.bits = bits;
    new_l.num = num;
    new_l.has_unknown = has_unknown;
    return new_l;
  }

  // ======= new Lconst returned =======================
  static from_pyrope(number_str) {
    // check, the input must be a string
    if (typeof number_str != 'string') {
      throw 'the input must be a string';
    }

    let txt = number_str.toLowerCase();
    txt = txt.replace(/_/g, '');

    // special cases
    if (txt === 'true') {
      return Lconst.new_lconst(false, 1, -1);
    } else if (txt == 'false') {
      return Lconst.new_lconst(false, 1, 0);
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
          if (sel_ch != 'b') {
            throw `ERROR: ${number_str} unknown pyrope encoding only binary can be signed 0sb...`;
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
          throw `ERROR: ${number_str} unknown pyrope encoding (leading ${sel_ch})...`;
        }
      }
    }

    let num = BigInt(0);
    let to_power = -1n;

    if (shift_mode === 10) {
      for (let i = skip_chars; i < txt.length; i++) {
        if (txt[i] >= 0) {
          num = 10n * num + BigInt(txt[i]);
        } else {
          throw `ERROR: ${number_str} encoding could not use ${txt[i]}`;
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
          throw `ERROR: ${number_str} encoding could not use ${txt[i]}`;
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
        throw `ERROR, ${number_str} negative value but it must be unsigned`;
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
        if (num !== 0n || unknown != 0n) {
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
        throw `ERROR: ${txt} binary encoding could not use ${char}`;
      }
    }

    if (!unsigned_result && negative) {
      // =====================================
      /* console.log('signed_result and num[0] is 1', num);
      console.log('len_num:', len_num); */
      num = num - (1n << len_num);
    }

    let return_Lconst = Lconst.new_lconst(
      false,
      Lconst.calc_num_bits(num),
      num,
      unknown_found
    );
    return_Lconst.unknown = unknown;
    return return_Lconst;
  }

  // restriction: only from decimal to pyrope
  to_pyrope() {
    let output = '0x';
    return output + this.num.toString(16);
  }

  adjust(com_lconst) {
    if (!(com_lconst instanceof Lconst)) {
      throw `ERROR the input ${com_lconst} must be an instance of Lconst`;
    }
    this.explicit_str =
      com_lconst.explicit_str && (this.bits === 0 || this.explicit_str);
    this.bits = Lconst.calc_num_bits(this.num);
  }

  // ========= operation =============
  // restriction: the num of these two objects do not have underscore and '?'
  and_op(com_lconst) {
    if (!(com_lconst instanceof Lconst)) {
      throw `ERROR the input ${com_lconst} must be an instance of Lconst`;
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
      throw `ERROR the input ${com_lconst} must be an instance of Lconst`;
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

  xor_op(com_lconst) {
    const num = this.num ^ com_lconst.num;
    return Lconst.new_lconst(false, Lconst.calc_num_bits(num), num);
  }
} // end of the class ————————————————————————————————————————————

// 🐕testing workspace for Lconst🐇
const a = Lconst.from_pyrope('0b010??1');
const b = Lconst.from_pyrope('0b000?11');
const res = a.or_op(b);
console.log(res);
module.exports = Lconst;
