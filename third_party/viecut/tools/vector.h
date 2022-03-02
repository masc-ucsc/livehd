/******************************************************************************
 * vector.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <vector>

class vector {
 public:
    template <typename T>
    static bool contains(const std::vector<T>& vec, const T& elem) {
        return (std::find(vec.begin(), vec.end(), elem) != vec.end());
    }
};
