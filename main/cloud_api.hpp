
#include "lgraph.hpp"
#include "main_api.hpp"

class Cloud_api {
protected:
  static void server(Eprp_var &var) {

    auto host = var.get("host");
    auto port = var.get("port");

    Main_api::warn(fmt::format("Still not implemented host:{} port:{}", host, port));
  }

  Cloud_api() {
  }
public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("cloud.server", "setup a lgshell server", &Cloud_api::server);
    m1.add_label_optional("host","host name for the cloud setup","localhost");
    m1.add_label_optional("port","port to use for the http request","80");

    eprp.register_method(m1);
  }

};

