//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#define CFG_ERR_1      "asdasdasd asd P{} asd asd"
#define CFG_NOT_FOUND  "asdasdasd asd P{} asd asd"

class Cfg_err {

public:
  static void error(error_id, varg args);
  static void warn(error_id, varg args);
  static void info(error_id, varg args);

};


#if 0
#define CORE_ERR_1      "asdasdasd asd P{} asd asd"
#define CORE_NOT_FOUND  "asdasdasd asd P{} asd asd"

// Future, another class for Core errors (diff file)
class Core_err {

public:
  static void error(error_id, varg args);
  static void warn(error_id, varg args);
  static void info(error_id, varg args);

};
#endif


