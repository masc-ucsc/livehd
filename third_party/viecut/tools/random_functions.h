/******************************************************************************
 * random_functions.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@univie.ac.at>
 * Copyright (C) 2017-2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

#include "common/definitions.h"

typedef std::mt19937 MersenneTwister;
static int m_seed;
static MersenneTwister m_mt;

class random_functions {
 public:
    random_functions() { }
    virtual ~random_functions() { }

    template <typename sometype>
    static void circular_permutation(std::vector<sometype>* v) {
        std::vector<sometype>& vec = *v;
        if (vec.size() < 2) return;
        for (unsigned int i = 0; i < vec.size(); i++) {
            vec[i] = i;
        }

        unsigned int size = vec.size();
        std::uniform_int_distribution<unsigned int> A(0, size - 1);
        std::uniform_int_distribution<unsigned int> B(0, size - 1);

        for (unsigned int i = 0; i < size; i++) {
            unsigned int posA = A(m_mt);
            unsigned int posB = B(m_mt);

            while (posB == posA) {
                posB = B(m_mt);
            }

            if (posA != vec[posB] && posB != vec[posA]) {
                std::swap(vec[posA], vec[posB]);
            }
        }
    }

    template <typename sometype>
    static void permutate_vector_fast(std::vector<sometype>* v, bool init) {
        std::vector<sometype>& vec = *v;
        if (init) {
            for (unsigned int i = 0; i < vec.size(); i++) {
                vec[i] = i;
            }
        }

        if (vec.size() < 10) return;

        int distance = 20;
        std::uniform_int_distribution<unsigned int> A(0, distance);
        unsigned int size = vec.size() - 4;
        for (unsigned int i = 0; i < size; i++) {
            unsigned int posA = i;
            unsigned int posB = (posA + A(m_mt)) % size;
            std::swap(vec[posA], vec[posB]);
            std::swap(vec[posA + 1], vec[posB + 1]);
            std::swap(vec[posA + 2], vec[posB + 2]);
            std::swap(vec[posA + 3], vec[posB + 3]);
        }
    }

    template <typename sometype>
    static void permutate_vector_local(std::vector<sometype>* v, bool init) {
        std::vector<sometype>& vec = *v;
        if (init) {
            for (unsigned int i = 0; i < vec.size(); i++) {
                vec[i] = i;
            }
        }

        size_t localsize = 128;
        for (size_t i = 0; i < vec.size(); i += localsize) {
            size_t end = std::min(vec.size(), i + localsize);
            std::shuffle(vec.begin() + i, vec.begin() + end, m_mt);
        }
    }

    template <typename sometype>
    static void permutate_vector_good(std::vector<sometype>* v) {
        std::vector<sometype>& vec = *v;

        unsigned int size = vec.size();
        if (size < 4) return;

        std::uniform_int_distribution<unsigned int> A(0, size - 4);
        std::uniform_int_distribution<unsigned int> B(0, size - 4);

        for (unsigned int i = 0; i < size; i++) {
            unsigned int posA = A(m_mt);
            unsigned int posB = B(m_mt);
            std::swap(vec[posA], vec[posB]);
            std::swap(vec[posA + 1], vec[posB + 1]);
            std::swap(vec[posA + 2], vec[posB + 2]);
            std::swap(vec[posA + 3], vec[posB + 3]);
        }
    }

    template <typename sometype>
    static void permutate_vector_good(std::vector<sometype>* v, bool init) {
        std::vector<sometype>& vec = *v;
        if (init) {
            for (unsigned int i = 0; i < vec.size(); i++) {
                vec[i] = (sometype)i;
            }
        }

        if (vec.size() < 10) {
            permutate_vector_good_small(v);
            return;
        }
        unsigned int size = vec.size();
        std::uniform_int_distribution<unsigned int> A(0, size - 4);
        std::uniform_int_distribution<unsigned int> B(0, size - 4);

        for (unsigned int i = 0; i < size; i++) {
            unsigned int posA = A(m_mt);
            unsigned int posB = B(m_mt);
            std::swap(vec[posA], vec[posB]);
            std::swap(vec[posA + 1], vec[posB + 1]);
            std::swap(vec[posA + 2], vec[posB + 2]);
            std::swap(vec[posA + 3], vec[posB + 3]);
        }
    }

    template <typename sometype>
    static void permutate_vector_good_small(std::vector<sometype>* v) {
        std::vector<sometype>& vec = *v;
        if (vec.size() < 2) return;
        unsigned int size = vec.size();
        std::uniform_int_distribution<unsigned int> A(0, size - 1);
        std::uniform_int_distribution<unsigned int> B(0, size - 1);

        for (unsigned int i = 0; i < size; i++) {
            unsigned int posA = A(m_mt);
            unsigned int posB = B(m_mt);
            std::swap(vec[posA], vec[posB]);
        }
    }

    static bool nextBool() {
        std::uniform_int_distribution<unsigned int> A(0, 1);
        return static_cast<bool>(A(m_mt));
    }

    // including lb and rb
    static unsigned nextInt(unsigned int lb, unsigned int rb) {
        std::uniform_int_distribution<unsigned int> A(lb, rb);
        return A(m_mt);
    }

    static double nextDouble(double lb, double rb) {
        std::uniform_real_distribution<> A(lb, rb);
        return A(m_mt);
    }

    static uint32_t next() {
        return m_mt();
    }

    static MersenneTwister getRand() {
        return m_mt;
    }

    static void setSeed(int seed) {
        m_seed = seed;
        srand(seed);
        m_mt.seed(m_seed);
    }

    static int getSeed() {
        return m_seed;
    }
};
