#ifndef NODEDELAY_H
#define NODEDELAY_H

#include <assert.h>

#include "dense.hpp"
#include "lgraphbase.hpp"

class Node_Delay {
private:
protected:
public:
  Node_Delay() {
    delay = 0;
  };
  float delay;
};

class LGraph_Node_Delay : virtual public LGraph_Base {
private:
  Dense<Node_Delay> node_delay;

protected:
  void node_delay_emplace_back();

public:
  LGraph_Node_Delay() = delete;
  explicit LGraph_Node_Delay(const std::string &path, const std::string &name) noexcept;
  virtual ~LGraph_Node_Delay(){};

  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  void  node_delay_set(Index_ID nid, float t);
  float node_delay_get(Index_ID nid) const;
};

#endif
