#include "VerilogVariable.hpp"

VerilogVariable::VerilogVariable() {

}

VerilogVariable::~VerilogVariable() {
  for(auto vList : this->vectorList) {
    delete vList;
  }

  for(auto aList : this->arrayList) {
    delete aList;
  }
}

void VerilogVariable::copyFrom(VerilogVariable* var_) {
  this->type       = var_->type;
  this->parity     = var_->parity;

  Indices* newPair;
  std::list<Indices*>::iterator it;
  for(it = var_->vectorList.begin(); it != var_->vectorList.end(); it++) {
    newPair = new Indices;
    newPair->left  = (*it)->left;
    newPair->right = (*it)->right;
    this->vectorList.push_back(newPair);
  }
}

