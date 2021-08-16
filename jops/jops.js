// CITATION
// https://tc39.es/proposal-bigint/#sec-exp-operator
// https://stackoverflow.com/questions/54758130/how-to-obtain-the-amount-of-bits-of-a-bigint



// 🐅🐅🐅testing workspace🐀🐀🐀

function BigIntnumberConversion(target = '0', from_base = 10, to_base = 10) {
  const numberString = parseInt(target, from_base).toString(to_base);
  /* console.log(target, "numberString", numberString) */
  return BigInt(numberString);
}

function isDigit(str){
  return /^\d+$/.test(str);
}

// console.log('answer', BigIntnumberConversion('1', -1));

class Lconst {
  constructor(number, explicit_str = false, bits = 0, num = 0) {
    this.number = number
    this.explicit_str = explicit_str; 
    this.bits = bits;
    this.num = num;
    this.initialized()
  }

  initialized(){
    if (this.number) {
      if (typeof this.number === "string") {
        this.explicit_str = false;
        this.bits = 0;
        this.num = 0;
      } else { //question: how to deal with the size of number is int64_t or Number(bigInt) 
        this.explicit_str = false;
        this.num = this.number;
        this.bits = Lconst.calc_num_bits(this.num);
      }
    }
  }
    // ======== support functions ======================
    static calc_num_bits(number) {
      const bigI = number > 0 ? BigInt(number) : -1n * BigInt(number);
      const binaryForm = bigI.toString(2);
      console.log(binaryForm);
      return binaryForm.length + 1;
    }

    static new_lconst(explicit_str, bits, num) {
      const new_l = new Lconst();
      new_l.explicit_str = explicit_str;
      new_l.bits = bits;
      new_l.num = num;
      return new_l;
    }

    // ======= new Lconst returned =======================
    static from_pyrope(number_str) {
      // check, the input must be a string
      if (typeof number_str != 'string') {
        throw 'the input must be a string';
      }

      const txt = number_str.toLowerCase();
      
      let skip_chars = 0;
      let shift_mode = -1;
      let negative = false;
      

      if ((txt.length >= skip_chars + 1) && isDigit(txt[skip_chars])) {
        shift_mode = 10;
        if (txt.length >= (2+skip_chars) && txt[skip_chars] === '0') {
          skip_chars += 1;
          const sel_ch = txt[skip_chars];

          if (sel_ch === 'x') {
            shift_mode = 16;
            skip_chars += 1;
          }          
        }
      }

      let num = BigInt(0);
      let to_power = -1n;
      for (let i = txt.length-1; i>= skip_chars; --i){
        if (txt[i] === '_') {
          continue;
        }
        to_power += 1n;
        // console.log(i + ' current letter is ' + txt[i] + ' shift mode ' + shift_mode + ' to power ' + to_power);
        num += BigIntnumberConversion(txt[i], shift_mode) * (BigInt(shift_mode)**to_power);
      }
      
      return Lconst.new_lconst(false, Lconst.calc_num_bits(num), num);
    } // end of from_pyrope

    // restriction: only from decimal to pyrope
    to_pyrope() {
      let output = '0x';
      return output + String(BigIntnumberConversion(this.num, 10, 16));
    }

    // ========= operation =============
    // restriction: the num of these two objects do not have underscore and '?'
    xor_op (com_lconst) {
      const num = this.num ^ com_lconst.num;
      return Lconst.new_lconst(false, Lconst.calc_num_bits(num), num);
    }
    sayHello() {
      console.log("hello")
    }
} // end of the class ————————————————————————————————————————————


// 🐕testing workspace for Lconst🐇

/* let testing = Lconst.from_pyrope('0x__ab_c_');
console.log('explicit_str: ', testing.explicit_str, 'bits: ', testing.bits, 'num: ', testing.num);
 */

/* let testing = Lconst.from_pyrope('0x12352353464564234526246345723564756345');
let testing2 = Lconst.from_pyrope('0x243523465546345765432545324564136161346'); */

let testing = Lconst.from_pyrope('0x1');
let testing2 = Lconst.from_pyrope('0x2');
let testing3 = testing.xor_op(testing2); 
console.log(testing3.to_pyrope());
module.exports = Lconst;