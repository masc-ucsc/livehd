//
//  client.cc
//
//  Copyright (c) 2012 Yuji Hirose. All rights reserved.
//  The Boost Software License 1.0
//

#include <iostream>

#include "httplib.h"

using namespace std;

int main(void) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  httplib::SSLClient cli("localhost", 8080);
#else
  httplib::Client cli("localhost", 1234);
#endif

  auto res = cli.Get("/hi");
  if (res) {
    cout << res->status << endl;
    cout << res->get_header_value("Content-Type") << endl;
    cout << res->body << endl;
  }

  string body(24, 'h');

  httplib::Request req;
  req.method = "POST";
  req.path   = "/lgraph/inou_yosys_fromlg";

  std::string host_and_port = "localhost:1234";

  req.headers.emplace("Host", host_and_port.c_str());
  req.headers.emplace("Accept", "*/*");
  req.headers.emplace("User-Agent", "lgraph/0.1");
  std::string boundary = "thisisjustarandomsequencethatshouldnot";
  std::string ctype    = "multipart/form-data; boundary=" + boundary;
  req.headers.emplace("Content-Type", ctype.c_str());

  std::string name1_body = "text default";
  std::string name2_body = std::string(1024, 'a');

  req.body = "--";
  req.body += boundary;
  req.body += "\r\nContent-Disposition: form-data; name=\"name1\"\r\n\r\n";
  req.body += name1_body;
  req.body += "\r\n";
  req.body += "--" + boundary;
  req.body += "\r\nContent-Disposition: form-data; name=\"name2\"\r\n\r\n";
  req.body += name2_body;
  req.body += "\r\n";
  req.body += "--" + boundary;
  req.body += "\r\n";

  auto res2    = std::make_shared<httplib::Response>();
  auto res2_ok = cli.send(req, *res2);

  if (res2_ok) {
    cout << res2->status << endl;
    cout << res2->get_header_value("Content-Type") << endl;
    cout << res2->body << endl;
  }

  return 0;
}

// vim: et ts=4 sw=4 cin cino={1s ff=unix
