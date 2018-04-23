
#include <iostream>

namespace lgraph {
  template <typename DataType>
    class PodArray {
      public:
#if 0
        // Only const access to nodeid
        class PodIterator {
          public:
            PodIterator(DataType *ptr): ptr(ptr){}
            PodIterator operator++() { PodIterator i(ptr); ++ptr; return i; }
            PodIterator operator--() { PodIterator i(ptr); --ptr; return i; }
            bool operator!=(const PodIterator & other) { return ptr != other.ptr; }
            DataType& operator*() const { return *ptr; }
          private:
            DataType *ptr;
        };
#endif
        class ConstPodIterator {
          public:
            ConstPodIterator(const DataType *ptr): ptr(ptr){}
            ConstPodIterator operator++() { ConstPodIterator i(ptr); ++ptr; return i; }
            ConstPodIterator operator--() { ConstPodIterator i(ptr); --ptr; return i; }
            bool operator!=(const ConstPodIterator & other) { return ptr != other.ptr; }
            const DataType& operator*() const { return *ptr; }
          private:
            const DataType *ptr;
        };
        class RConstPodIterator {
          public:
            RConstPodIterator(const DataType *ptr): ptr(ptr){}
            RConstPodIterator operator++() { RConstPodIterator i(ptr); --ptr; return i; } // reverse
            RConstPodIterator operator--() { RConstPodIterator i(ptr); ++ptr; return i; } // reverse
            bool operator!=(const RConstPodIterator & other) { return ptr != other.ptr; }
            const DataType& operator*() const { return *ptr; }
          private:
            const DataType *ptr;
        };
      private:
        unsigned len;
        DataType val[16];
      public:
        PodArray() {
          len = 0;
        }

        ConstPodIterator begin() const { return ConstPodIterator(val); }
        ConstPodIterator end() const { return ConstPodIterator(val + len); }

#if 0
        PodIterator begin() { return PodIterator(val); }
        PodIterator end() { return PodIterator(val + len); }
#endif

        RConstPodIterator rbegin(uint8_t p) const { return RConstPodIterator(val+len-p-1); }
        RConstPodIterator rbegin() const { return RConstPodIterator(val+len-1); }
        RConstPodIterator rend() const { return RConstPodIterator(val-1); }

        void push(DataType p) {
          val[len++] = p;
          if (len>=15)
            len = 15;
        }

        // rest of the container definition not related to the question ...
    };

  template<class T>
    class ReverseAdapter {
      public:
        ReverseAdapter(T& container, uint8_t p) : m_container(container) { pos = p; }
        typename T::RConstPodIterator begin() { return m_container.rbegin(pos); }
        typename T::RConstPodIterator end() { return m_container.rend(); }

      private:
        uint8_t pos;
        T& m_container;
    };

  template<class T>
    class ConstReverseAdapter {
      public:
        ConstReverseAdapter(const T& container, uint8_t p) : m_container(container) { pos = p ;}
        typename T::RConstPodIterator begin() { return m_container.rbegin(pos); }
        typename T::RConstPodIterator end() { return m_container.rend(); }

      private:
        uint8_t pos;
        const T& m_container;
    };

  template<class T>
    ReverseAdapter<T> reverse(T& container, uint8_t pos=0) {
      return ReverseAdapter<T>(container,pos);
    }

  template<class T>
    ConstReverseAdapter<T> reverse(const T& container, uint8_t pos=0) {
      return ConstReverseAdapter<T>(container,pos);
    }
};

int main() {
  lgraph::PodArray<char> array;

  array.push('a');
  array.push('b');
  array.push('c');

  for(auto& c : array) {
    std::cout <<"char: " << c << std::endl;
    // nid == nodeid
    // graph.cell.get(nid).getOperation() == FlopOp
    // graph.cell.get(nid).get()
    // for( auto & i: graph.inputs(nid))
    //    i.port i.nid
    // for( auto & o: graph.outputs(nid))
    //    o.port o.nid
    // for( auto & o: graph.out_ports(nid))
    //    0 1 2
    // for( auto & o: graph.inp_ports(nid))
    //    0 1 2
  }

#if 0
  for(auto& c : array) {
    // Increase all by one
    c+=2;
  }
#endif

  array.push('d');

  std::cout << "Going again\n";

  for(const auto& c : lgraph::reverse(array))
    std::cout <<"char: " << c << std::endl;

  std::cout << "Going again\n";

  for(const auto& c : lgraph::reverse(array,1))
    std::cout <<"char: " << c << std::endl;

  return 0;
}

