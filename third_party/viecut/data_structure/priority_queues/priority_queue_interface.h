/******************************************************************************
 * priority_queue_interface.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@kit.edu>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include "common/definitions.h"

class priority_queue_interface {
 public:
    priority_queue_interface() { }
    virtual ~priority_queue_interface() { }

    /* returns the size of the priority queue */
    virtual NodeID size() = 0;
    virtual bool empty() = 0;

    virtual void insert(NodeID id, Gain gain) = 0;

    virtual Gain maxValue() = 0;
    virtual NodeID maxElement() = 0;
    virtual NodeID deleteMax() = 0;

    virtual void decreaseKey(NodeID node, Gain newGain) = 0;
    virtual void increaseKey(NodeID node, Gain newKey) = 0;

    virtual void changeKey(NodeID element, Gain newKey) = 0;
    virtual Gain getKey(NodeID element) = 0;
    virtual void deleteNode(NodeID node) = 0;
    virtual bool contains(NodeID node) = 0;
};
