//
//  hello.cc
//
//  Copyright (c) 2012 Yuji Hirose. All rights reserved.
//  The Boost Software License 1.0
//

#include <iostream>

#include "httplib.h"

// Sample command to upload data
// time http -f POST localhost:1234/multipart name1@~/tmp/pp4.cpp

using namespace httplib;

int main(void) {
  Server svr;

  svr.Get("/hi", [](const Request& /*req*/, Response& res) { res.set_content("Hello World!", "text/plain"); })
      .Post(R"(/lgraph/(\w+))", [&](const auto& req, auto& res) {
        auto cmd = req.matches[1];

        const auto& file = req.get_file_value("name1");

        std::cout << " cmd:" << cmd << std::endl;
        if (file.length) {
          std::cout << "name1.file:" << file.filename << " header:" << req.get_header_value("Content-Type")
                    << " offset:" << file.offset << " size:" << file.length << " type:" << file.content_type << std::endl;
          // file.filename;
          // file.content_type;
          auto body = req.body.substr(file.offset, file.length);
          std::cout << " body:" << body << std::endl;
        }

        const auto& name2 = req.get_file_value("name2");
        if (name2.length) {
          auto name2_body = req.body.substr(name2.offset, name2.length);
          std::cout << "name2.file:"
                    << " size:" << name2.length << " body:" << name2_body << std::endl;
        }

        sleep(2);  // Simulate synthesis

        res.set_content("thanks!", "text/plain");
      });

  svr.listen("localhost", 1234);
}

// vim: et ts=4 sw=4 cin cino={1s ff=unix
