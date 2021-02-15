/* -*- Mode: C++ ; indent-tabs-mode: nil ; c-file-style: "stroustrup" -*-

   Rapid Prototyping Floorplanner Project
   Author: Greg Faust

   File:   MathUtil.hh     C++ Header File for Math Helpers for FloorPlanner.

*/

#pragma once

#include <iostream>
using namespace std;

// Some other useful routines.
int GCD(int x, int y);

class Primes {
private:
  static bool* candidateArray;  // Keeps track of whether int is still a possible prime.

  static constexpr int candidateArrayLen = 100000;  // The max length of candidateArray.
                                                    // As indicated, used properly, primes to 100,000 are enough to find prime
                                                    // factorization of any 32 bit int.

  static int* primeArray;  // Store the primes as we find them.

  static constexpr int primeArrayLen = 10000;  // The max length of primeArray.
                                               // There are almost 10,000 primes less than 100,000.

  static int currMaxPrimeInx;  // Next available empty spot in primeArray.
  static int currMaxPrime;     // Highest prime we have found so far.
  static int finalMaxPrime;    // This stores the max prime we can find within size of candidate array.
  static int currPrimeInx;     // Current spot in iteration through primeArray.

  static void initPrimes();
  static int  findNextPrime();

public:
  static constexpr int primeError = -1;  // Value to return when another prime cannot be found.
  static void          resetPrimes();
  static int           getNextPrime();
};  // End Class Primes

// Not much to this.  Just the prime number and its exponent.  Make them read only.
struct primeFactor {
private:
  int prime;
  int exp;

public:
  int Prime() const { return prime; }
  int Exp() const { return exp; }

  void setFactor(int primeIn, int expIn) {
    prime = primeIn;
    exp   = expIn;
  }

  primeFactor() {}
  primeFactor(int primeIn, int expIn) : prime(primeIn), exp(expIn) {}

};  // End primeFactor.

class primeFactorization {
private:
  int number;
  int currMaxFactor;

  static constexpr int factorArrayLen = 10;
  primeFactor*         factorization;

  void addFactor(int p, int e) { factorization[currMaxFactor++].setFactor(p, e); }

public:
  primeFactorization(int num);
  ~primeFactorization();
  int  countFactors() const;
  void expandFactors(int* factors) const;
  void printFactors(ostream& s) const;

};  // End primeFactorization.

int balanceFactors(int composite, double ratio = 1.0);
