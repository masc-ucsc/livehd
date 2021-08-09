// CITATION
// https://tc39.es/proposal-bigint/#sec-exp-operator
// https://stackoverflow.com/questions/54758130/how-to-obtain-the-amount-of-bits-of-a-bigint



// 🐅🐅🐅testing workspace🐀🐀🐀
const truer = 1;

/* static Bits_t calc_num_bits(const Number &num) {
  if (num == 0 || num == -1)
    return 1;
  if (num > 0)
    return msb(num) + 2;  // +2 because values are signed (msb==0 is 1 bit)
  return msb(-num - 1) + 2;
} */
function calc_num_bits(number) {
  bigI = number > 0 ? BigInt(number) : -1n * BigInt(number); // Question: what if the number is negative?????
  const binaryForm = bigI.toString(2);
  console.log(`binary form is ${binaryForm}`);
  console.log(`the number of bits is ${binaryForm.length} + 1 [one bit for sign]`);
  return binaryForm.length + 1; // Question: one more bits for the sign?????
}

calc_num_bits(-10);


/**
 * to use bigInt, simply add 'n' behind a number; for example, 2n 
 */
class Lconst {
  constructor(number) {
    this.number = number
    this.explicit_str = false; 
    this.bits = 0;
    this.num = 0;
    this.initialized()
  }

  static char_to_bits = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  
  static char_to_val = [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1];
  
    static calc_num_bits(number) {
      bigI = number > 0 ? BigInt(number) : -1n * BigInt(number); // Question: what if the number is negative?????
      const binaryForm = bigI.toString(2);
      console.log(`binary form is ${binaryForm}`);
      console.log(`the number of bits is ${binaryForm.length} + 1 [one bit for sign]`);
      return binaryForm.length + 1; // Question: one more bits for the sign?????
    }

  initialized(){
    if (!this.number) {
      this.explicit_str = false;
      this.bits = 0;
      this.num = 0;
    } else { //question: how to deal with the size of number is int64_t or Number(bigInt) 
      this.explicit_str = false;
      this.num = this.number;
      this.bits = Lconst.calc_num_bits(this.num)
    }
  }
} // end of the class

// testing workspace for Lconst
/* let testing = new Lconst(3);
console.log(testing.num, " ", testing.bits); */