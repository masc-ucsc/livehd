//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string>

#include "pass.hpp"

//************************************
//***** Start Def Class Definition ***
//************************************

class Def_conn {
public:
  std::string pin_name;
  std::string compo_name;
};

class Def_net {
public:
  std::string           name;
  std::vector<Def_conn> conns;
};

class Def_row {
public:
  std::string name;
  std::string site;
  int         origx;  // specifies the location of the first site in the row.
  int         origy;
  std::string orient;
  int         numx;  // specifies a repeating set of sites that create the row.
  int         numy;
  int         stepx;
  int         stepy;
};

class Def_track {
public:
  std::string              direction;
  int                      location;
  int                      num_tracks;
  int                      space;
  std::vector<std::string> layers;
};

class Def_component {
public:
  std::string name;
  std::string macro_name;
  int         posx;
  int         posy;
  std::string orientation;
  bool        is_fixed  = false;
  bool        is_placed = false;
};

class Def_io {
public:
  typedef uint16_t pos_type;
  typedef enum { input, output } Direction;

  typedef struct {
    std::string metal_name;
    pos_type    xl, yl, xh, yh;
  } Physical_pin;

  std::string  io_name;
  std::string  net_name;
  Direction    dir;
  int          posx;
  int          posy;
  Physical_pin phy;
};

class Def_info {
private:
protected:
public:
  std::string                mod_name;
  std::vector<Def_row>       rows;
  std::vector<Def_track>     tracks;
  std::vector<Def_component> compos;
  std::vector<Def_net>       nets;
  std::vector<Def_io>        ios;
};

//*************************************
//***** End of Def Class Definition ***
//*************************************
