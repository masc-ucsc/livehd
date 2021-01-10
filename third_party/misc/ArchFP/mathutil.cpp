/* -*- Mode: C++ ; indent-tabs-mode: nil ; c-file-style: "stroustrup" -*-

   Rapid Prototyping Floorplanner Project
   Author: Greg Faust

   File:   MathUtil.cc     C++ File for Math Helpers for FloorPlanner.

*/

#include "mathutil.hpp"

#include <climits>
#include <cmath>
#include <cstdlib>

/*
  This will be some functions surrounding primes.

  Start with a class that holds a cache of the primes known to date.  This will lazy discover new primes as needed.
  We will use the Sieve of Eratosthenes.
  Keep the array of non-prime numbers (candidates) around.
  TODO:   Use logicals for now, but could use bits if size becomes an issue.
          Since we only ever need one of these objects, essentially everything will be static.

  Note: that to be able to prime factor all (signed or unsigned) 32 bit ints requires all primes up to
  sqrt(10^10) = 100,000.  Smallest prime factor is guaranteed to be in this list.  There may be prime factors
  greater than 100,000, but if we find no more prime factors less than 100,000, then the residue, whatever
  it is, must be prime.

  Note:   None of this is meant to be useful for LARGE number factoring such as useful in encryption algorithms.

*/

static bool primesInitialized = false;

// If I make this static, it will be magically called when any other member is called.
// However, then I can't send it any arguments.  Unclear what is best here.
// Indicate we are now initialized.

// As indicated, used properly, primes to 100,000 are enough to find prime factorization of any 32 bit int.
int   Primes::candidateArrayLen = 100000;
bool *Primes::candidateArray    = new bool[candidateArrayLen];

// There are almost 10,000 primes less than 100,000.
int  Primes::primeArrayLen = 10000;
int *Primes::primeArray    = new int[primeArrayLen];

int Primes::currMaxPrimeInx = 0;
int Primes::currPrimeInx    = 0;
int Primes::finalMaxPrime   = INT_MAX;  // Start out with this artificially high until we hit the ceiling.
int Primes::currMaxPrime    = 0;        // Start out with a dummy first prime.

// Initialize to TRUE, meaning the number is still a possible prime.
// except 0 and 1 which aren't prime.

void Primes::initPrimes() {
  candidateArray[0] = false;
  candidateArray[1] = false;
  for (int i = 2; i < candidateArrayLen; i++) candidateArray[i] = true;
}

// This is the lazy generator.

int Primes::findNextPrime() {
  // Look for the next prime in candidate array.
  int nextPrime = primeError;
  for (int i = currMaxPrime + 1; i < candidateArrayLen; i++)
    if (candidateArray[i]) {
      nextPrime = i;
      break;
    }
  // If we couldn't find any additional primes within size of candidate array.  Set maxPrime and return.
  if (nextPrime == primeError) {
    // Perhaps we should also release the candidate array, as we will no longer need it.
    finalMaxPrime = currMaxPrime;
    return primeError;  // Indicate we couldn't find another prime.
  }
  // We found the next prime.  Go ahead and get rid of its multiples now.
  // NOTE     This is iterating through multiples, not incrementing by 1.
  // TODO.	Should we wait until the next one?
  //			This way we will always clear out one more than needed.
  //			But if we do this later, we will need to unroll this loop for 2 into the initialization code.
  for (int i = nextPrime * 2; i < candidateArrayLen; i += nextPrime) candidateArray[i] = false;
  // Remember we found a new high prime.
  primeArray[currMaxPrimeInx++] = nextPrime;
  currMaxPrime                  = nextPrime;
  return nextPrime;
}

// These next two usually just go through the prime array unless a larger prime than currently known is needed.
void Primes::resetPrimes() {
  if (!primesInitialized)
    initPrimes();
  currPrimeInx = 0;
}

int Primes::getNextPrime() {
  // If we need to find a new prime, do so.
  if (currPrimeInx == currMaxPrimeInx) {
    currPrimeInx += 1;
    return findNextPrime();
  }
  // Otherwise, just pull the next prime out of the cache of found primes.
  else
    return primeArray[currPrimeInx++];
}

primeFactorization::primeFactorization(int num) {
  // Keep what number we are the factorization for!!
  number = num;
  // How to figure out how long to make the array??
  // For now use the following reasoning.
  // 2*3*5*7*11*13*17*19*23*29 = 6,469,693,230 > any 32 bit int.
  // Therefore, a 32 bit int can not possibly have more than 10 DIFFERENT prime factors.
  factorArrayLen = 10;
  factorization  = (primeFactor *)(malloc(sizeof(primeFactor) * factorArrayLen));
  currMaxFactor  = 0;
  // Start by getting some facts about num.
  int posnum  = abs(num);
  int sqrtnum = (int)ceil(sqrt((double)posnum));

  // Now find a prime factor and add to its exponent.
  int p;
  Primes::resetPrimes();
  while ((p = Primes::getNextPrime()) <= sqrtnum) {
    // If we are at the last one, break.
    if (p == Primes::primeError)
      break;
    int count = 0;
  tryAgain:
    // See if there is (another) factor of the current prime.
    if (posnum % p == 0) {
      count++;
      posnum = posnum / p;
      goto tryAgain;
    }
    if (count > 0)
      addFactor(p, count);
    // Get out of the loop early if we have found the entire factorization.
    if (posnum == 1)
      break;
  }

  // We may need to add a final factor that was greater than our max prime.
  // We will also get here if num is prime.
  if (posnum != 1)
    addFactor(posnum, 1);
}

primeFactorization::~primeFactorization() { free(factorization); }

// This expands the prime factorization into an integer array of the primes.
// Each prime is repeated the appropriate number of times in the array.
// Ergo, multiplying together all of the factors in the array will recreate the original number.
int primeFactorization::countFactors() {
  int factorCount = 0;
  for (int i = 0; i < currMaxFactor; i++) factorCount += factorization[i].Exp();
  return factorCount;
}

void primeFactorization::expandFactors(int *factors) {
  // Fill the array with the factors.
  int factorInx = 0;
  for (int i = 0; i < currMaxFactor; i++) {
    primeFactor pf = factorization[i];
    for (int j = 0; j < pf.Exp(); j++) factors[factorInx++] = pf.Prime();
  }
}

void primeFactorization::printFactors(ostream &o) {
  o << number << "=" << factorization[0].Prime();
  for (int i = 0; i < currMaxFactor; i++) {
    primeFactor pf = factorization[i];
    for (int j = 0; j < pf.Exp(); j++) {
      // We already printed out the first factor.
      if (i == 0 and j == 0)
        continue;
      o << "*" << pf.Prime();
    }
  }

  // Output a new line.
  o << "\n";
}

// Treat the boolean array as if it were a binary number.
// To increment it, we must perform the carry operation.
void incrementBoolArray(bool *arr, int len) {
  for (int i = 0; i < len; i++) {
    arr[i] = !arr[i];
    if (arr[i])
      break;
  }
}

// This takes in a number that is hopefully composite.
// And returns a number that divides the number, and is close to the requested
//    ratio between the two factors.
// The second factor is easily derived from the return value by composite/retval.
// Ratio greater than 1 will return the max of the two factors.
// Ratio less than or equal to 1 will return the min of the two factors.
// Given the small number of factors likely to be used, try ALL combinations to find one closest to ratio.
// Given that the new "distance" calculation works better for ratios > 1, we will flip and flip back if needed.
int balanceFactors(int composite, double targetRatio) {
  // First get the prime factorization for the number.
  primeFactorization pf = primeFactorization(composite);

  // Get an array of the factors of the number.
  int  len       = pf.countFactors();
  int *factArray = (int *)malloc(sizeof(int) * len);
  pf.expandFactors(factArray);

  // Allocate an array of booleans to dictate which factor combination we are about to try.
  // Initializing to false is the equivalent of "zero".
  bool *boolArr = (bool *)malloc(sizeof(bool) * len);
  for (int i = 0; i < len; i++) boolArr[i] = false;

  bool   flip  = (targetRatio < 1);
  double ratio = flip ? 1 / targetRatio : targetRatio;

  // Dummy value to make sure first combo will set new curRatio.
  double curMetric = 1000000000;
  int    curf1     = 1;
  int    curf2     = 1;
  // We are dealing with the power set of the number of factors.
  int combos = 1 << len;
  for (int i = 0; i < combos; i++) {
    int f1 = 1;
    int f2 = 1;
    for (int j = 0; j < len; j++) {
      if (boolArr[j])
        f1 *= factArray[j];
      else
        f2 *= factArray[j];
    }
    incrementBoolArray(boolArr, len);
    double newRatio = ((double)f1) / f2;
    if (newRatio < 1)
      continue;

    // Is this better than best so far?
    double newMetric = (newRatio > ratio) ? newRatio / ratio : ratio / newRatio;
    if (newMetric < curMetric) {
      curMetric = newMetric;
      curf1     = f1;
      curf2     = f2;
    }
  }
  free(factArray);
  free(boolArr);

  int retval = flip ? curf2 : curf1;
  // cout << "Composite=" << composite << " ratio=" << targetRatio << " retval=" << retval << " other=" << composite/retval << "
  // actual=" << retval/((double)composite/retval) << "\n";
  return retval;
}

// Calculate GCD using Euclid's algorithm
// This seems faster than getting the prime factorization and doing it that way.
int GCD(int x, int y) {
  int t;
  while (y != 0) {
    t = y;
    y = x % y;
    x = t;
  }
  return x;
}
