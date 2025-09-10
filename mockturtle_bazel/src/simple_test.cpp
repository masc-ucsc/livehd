// src/simple_test.cpp
#include <mockturtle/networks/mig.hpp>
#include <iostream>

using namespace mockturtle;

int main() {
    mig_network mig;
    
    // Create primary inputs
    auto a = mig.create_pi();
    auto b = mig.create_pi();
    
    // Create an AND gate
    auto f = mig.create_and(a, b);
    
    // Create primary output
    mig.create_po(f);
    
    std::cout << "Created MIG network with " << mig.size() << " nodes\n";
    return 0;
}
