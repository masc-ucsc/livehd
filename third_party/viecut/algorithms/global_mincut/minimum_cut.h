/******************************************************************************
 * minimum_cut.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <memory>

#include "common/definitions.h"
#include "data_structure/graph_access.h"

class minimum_cut {
 public:
    virtual ~minimum_cut() { }

    virtual EdgeWeight perform_minimum_cut(graphAccessPtr, bool) {
        return perform_minimum_cut();
    }

    virtual EdgeWeight perform_minimum_cut(mutableGraphPtr, bool) {
        return perform_minimum_cut();
    }

    virtual EdgeWeight perform_minimum_cut(graphAccessPtr) {
        return perform_minimum_cut();
    }

    virtual EdgeWeight perform_minimum_cut(mutableGraphPtr) {
        return perform_minimum_cut();
    }

    virtual EdgeWeight perform_minimum_cut() {
        exit(1);
        return 42;
    }
};
