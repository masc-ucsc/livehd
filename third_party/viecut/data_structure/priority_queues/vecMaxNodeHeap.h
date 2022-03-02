/******************************************************************************
 * vecMaxNodeHeap.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@kit.edu>
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <execinfo.h>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data_structure/priority_queues/maxNodeHeap.h"
#include "data_structure/priority_queues/priority_queue_interface.h"

typedef EdgeWeight Key;

class vecMaxNodeHeap : public priority_queue_interface {
 public:
    struct Data {
        NodeID node;
        explicit Data(NodeID node) : node(node) { }
    };

    typedef QElement<Data> PQElement;

    vecMaxNodeHeap(NodeID num_nodes, EdgeWeight) : vecMaxNodeHeap(num_nodes) { }
    explicit vecMaxNodeHeap(NodeID num_nodes)
        : m_element_index(num_nodes, UNDEFINED_NODE) { }
    virtual ~vecMaxNodeHeap() { }

    NodeID size();
    bool empty();

    bool contains(NodeID node);
    void insert(NodeID id, Gain gain);

    NodeID deleteMax();
    void deleteNode(NodeID node);
    NodeID maxElement();
    Gain maxValue();

    void decreaseKey(NodeID node, Gain gain);
    void increaseKey(NodeID node, Gain gain);
    void changeKey(NodeID node, Gain gain);
    Gain getKey(NodeID node);

 private:
    std::vector<PQElement> m_elements;
    std::vector<NodeID> m_element_index;
    std::vector<std::pair<Key, int> > m_heap;

    void siftUp(int pos);
    void siftDown(int pos);
};

inline Gain vecMaxNodeHeap::maxValue() {
    return m_heap[0].first;
}

inline NodeID vecMaxNodeHeap::maxElement() {
    return m_elements[m_heap[0].second].get_data().node;
}

inline void vecMaxNodeHeap::siftDown(int pos) {
    Gain curKey = m_heap[pos].first;
    int lhsChild = 2 * pos + 1;
    int rhsChild = 2 * pos + 2;
    if (rhsChild < static_cast<int>(m_heap.size())) {
        Gain lhsKey = m_heap[lhsChild].first;
        Gain rhsKey = m_heap[rhsChild].first;
        if (lhsKey < curKey && rhsKey < curKey) {
            return;             // we are done
        } else {
            // exchange with the larger one (maxHeap)
            int swap_pos = lhsKey > rhsKey ? lhsChild : rhsChild;
            std::swap(m_heap[pos], m_heap[swap_pos]);

            int element_pos = m_heap[pos].second;
            m_elements[element_pos].set_index(pos);

            element_pos = m_heap[swap_pos].second;
            m_elements[element_pos].set_index(swap_pos);

            siftDown(swap_pos);
            return;
        }
    } else if (lhsChild < static_cast<int>(m_heap.size())) {
        if (m_heap[pos].first < m_heap[lhsChild].first) {
            std::swap(m_heap[pos], m_heap[lhsChild]);

            int element_pos = m_heap[pos].second;
            m_elements[element_pos].set_index(pos);

            element_pos = m_heap[lhsChild].second;
            m_elements[element_pos].set_index(lhsChild);

            siftDown(lhsChild);
            return;
        } else {
            return;             // we are done
        }
    }
}

inline void vecMaxNodeHeap::siftUp(int pos) {
    if (pos > 0) {
        int parentPos = static_cast<int>(pos - 1) / 2;
        if (m_heap[parentPos].first < m_heap[pos].first) {
            // heap condition not fulfulled
            std::swap(m_heap[parentPos], m_heap[pos]);

            int element_pos = m_heap[pos].second;
            m_elements[element_pos].set_index(pos);

            // update the heap index in the element
            element_pos = m_heap[parentPos].second;
            m_elements[element_pos].set_index(parentPos);

            siftUp(parentPos);
        }
    }
}

inline NodeID vecMaxNodeHeap::size() {
    return m_heap.size();
}

inline bool vecMaxNodeHeap::empty() {
    return m_heap.empty();
}

inline void vecMaxNodeHeap::insert(NodeID node, Gain gain) {
    if (m_element_index[node] == UNDEFINED_NODE) {
        int element_index = m_elements.size();
        int heap_size = m_heap.size();

        m_elements.push_back(PQElement(Data(node), gain, heap_size));
        m_heap.push_back(std::pair<Key, int>(gain, element_index));
        m_element_index[node] = element_index;
        siftUp(heap_size);
    }
}

inline void vecMaxNodeHeap::deleteNode(NodeID node) {
    int element_index = m_element_index[node];
    int heap_index = m_elements[element_index].get_index();

    m_element_index[node] = UNDEFINED_NODE;

    std::swap(m_heap[heap_index], m_heap[m_heap.size() - 1]);
    // update the position of its element in the element array
    m_elements[m_heap[heap_index].second].set_index(heap_index);

    // we dont want holes in the elements array
    // delete the deleted element from the array
    if (element_index != static_cast<int>(m_elements.size() - 1)) {
        std::swap(m_elements[element_index], m_elements[m_elements.size() - 1]);
        m_heap[m_elements[element_index].get_index()].second = element_index;
        int cnode = m_elements[element_index].get_data().node;
        m_element_index[cnode] = element_index;
    }

    m_elements.pop_back();
    m_heap.pop_back();

    if (m_heap.size() > 1 && heap_index < static_cast<int>(m_heap.size())) {
        // fix the max heap property
        siftDown(heap_index);
        siftUp(heap_index);
    }
}

inline NodeID vecMaxNodeHeap::deleteMax() {
    if (m_heap.size() > 0) {
        int element_index = m_heap[0].second;
        int node = m_elements[element_index].get_data().node;
        m_element_index[node] = UNDEFINED_NODE;

        m_heap[0] = m_heap[m_heap.size() - 1];
        // update the position of its element in the element array
        m_elements[m_heap[0].second].set_index(0);

        // we dont want holes in the elements array
        // delete the deleted element from the array
        if (element_index != static_cast<int>(m_elements.size() - 1)) {
            m_elements[element_index] = m_elements[m_elements.size() - 1];
            m_heap[m_elements[element_index].get_index()].second
                = element_index;
            int cnode = m_elements[element_index].get_data().node;
            m_element_index[cnode] = element_index;
        }

        m_elements.pop_back();
        m_heap.pop_back();

        if (m_heap.size() > 1) {
            // fix the heap property
            siftDown(0);
        }

        return node;
    }

    return -1;
}

inline void vecMaxNodeHeap::changeKey(NodeID node, Gain gain) {
    Gain old_gain = m_heap[m_elements[m_element_index[node]].get_index()].first;
    if (old_gain > gain) {
        decreaseKey(node, gain);
    } else if (old_gain < gain) {
        increaseKey(node, gain);
    }
}

inline void vecMaxNodeHeap::decreaseKey(NodeID node, Gain gain) {
    VIECUT_ASSERT_TRUE(m_element_index[node] != UNDEFINED_NODE);
    int queue_idx = m_element_index[node];
    int heap_idx = m_elements[queue_idx].get_index();
    m_elements[queue_idx].set_key(gain);
    m_heap[heap_idx].first = gain;
    siftDown(heap_idx);
}

inline void vecMaxNodeHeap::increaseKey(NodeID node, Gain gain) {
    VIECUT_ASSERT_TRUE(m_element_index[node] != UNDEFINED_NODE);
    int queue_idx = m_element_index[node];
    int heap_idx = m_elements[queue_idx].get_index();
    m_elements[queue_idx].set_key(gain);
    m_heap[heap_idx].first = gain;
    siftUp(heap_idx);
}

inline Gain vecMaxNodeHeap::getKey(NodeID node) {
    return m_heap[m_elements[m_element_index[node]].get_index()].first;
}

inline bool vecMaxNodeHeap::contains(NodeID node) {
    return m_element_index[node] != UNDEFINED_NODE;
}
