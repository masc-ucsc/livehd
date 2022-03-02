/******************************************************************************
 * timer.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@univie.ac.at>
 *
 *****************************************************************************/

#pragma once

#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

class timer {
 public:
    timer() {
        m_start = timestamp();
    }

    void restart() {
        m_start = timestamp();
    }

    double elapsed() const {
        return timestamp() - m_start;
    }

    double elapsedToZero() {
        double elapsed = this->elapsed();
        restart();
        return elapsed;
    }

 private:
    /** Returns a timestamp ('now') in seconds (incl. a fractional part). */
    inline double timestamp() const {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        return static_cast<double>(tp.tv_sec) + tp.tv_usec / 1000000.;
    }

    double m_start;
};
