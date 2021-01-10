/* -*- Mode: C++ ; indent-tabs-mode: nil ; c-file-style: "stroustrup" -*-

   Rapid Prototyping Floorplanner Project
   Author: Greg Faust

   File:   MathUtil.hh     C++ Header File for Math Helpers for FloorPlanner.

*/

#include <iostream>
using namespace std;

// Apparently, we need to define min and max !!
#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

// Some other useful routines.
int GCD(int x, int y);

class Primes {
private:
  static bool* candidateArray;     // Keeps track of whether int is still a possible prime.
  static int   candidateArrayLen;  // The max length of candidateArray.
  static int*  primeArray;         // Store the primes as we find them.
  static int   primeArrayLen;      // The max length of primeArray.
  static int   currMaxPrimeInx;    // Next available empty spot in primeArray.
  static int   currMaxPrime;       // Highest prime we have found so far.
  static int   finalMaxPrime;      // This stores the max prime we can find within size of candidate array.
  static int   currPrimeInx;       // Current spot in iteration through primeArray.

  static void initPrimes();
  static int  findNextPrime();

public:
  static const int primeError = -1;  // Value to return when another prime cannot be found.
  static void      resetPrimes();
  static int       getNextPrime();
};  // End Class Primes

// Not much to this.  Just the prime number and its exponent.  Make them read only.
struct primeFactor {
private:
  int prime;
  int exp;

public:
  int Prime() { return prime; }
  int Exp() { return exp; }

  void setFactor(int primeIn, int expIn) {
    prime = primeIn;
    exp   = expIn;
  }

  primeFactor(int primeIn, int expIn) {
    prime = primeIn;
    exp   = expIn;
  }

};  // End primeFactor.

class primeFactorization {
private:
  int          number;
  int          factorArrayLen;
  int          currMaxFactor;
  primeFactor* factorization;

  void addFactor(int p, int e) { factorization[currMaxFactor++].setFactor(p, e); }

public:
  primeFactorization(int num);
  ~primeFactorization();
  int  countFactors();
  void expandFactors(int* factors);
  void printFactors(ostream& s);

};  // End primeFactorization.

int balanceFactors(int composite, double ratio = 1.0);
