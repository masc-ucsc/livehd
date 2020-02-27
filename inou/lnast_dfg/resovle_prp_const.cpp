#include <string_view>
#include "inou_lnast_dfg.hpp"

Node Inou_lnast_dfg::resolve_constant(LGraph *g, std::string_view str) {
  // arguments -> local variable
  // bool is_in32b;
  // bool is_explicit_signed;
  // bool has_bool_dc;
  // bool is_pure_dc;
  bool     is_signed = false;
  uint32_t explicit_bits;
  size_t   bit_width;

  std::string token1st, token2nd;
  std::string str_in(str);//explicitly construct a string from string_view
  char        rm     = '_'; // remove '_' in 0xF___FFFF
  str_in.erase(std::remove(str_in.begin(), str_in.end(), rm), str_in.end());
  size_t s_pos = str_in.find('s'); // O(n)
  size_t u_pos = 0;
  size_t idx;

  // extract 1st and 2nd tokens, delimiter: s or u
  if(s_pos != std::string::npos) {
    token1st  = str_in.substr(0, s_pos);
    token2nd  = str_in.substr(s_pos + 1);
    is_signed = true;
    // is_explicit_signed = true;
  } else {
    u_pos = str_in.find('u'); // O(n)
    if(u_pos != std::string::npos) {
      token1st = str_in.substr(0, u_pos);
      token2nd = str_in.substr(u_pos + 1);
      // is_explicit_signed = true;
    } else
      token1st = str_in;
  }

  // explicit bits width
  if(!token2nd.empty()) {
    explicit_bits = (uint32_t)std::stoi(token2nd);
  } else
    explicit_bits = 0;

  // main process for bin/hex/dec
  // hex
  if(token1st[0] == '0' && token1st[1] == 'x') {
    token1st = token1st.substr(2); // exclude leading hex 0x
    idx      = token1st.find_first_not_of('0');
    token1st = token1st.substr(idx); // exclude leading 0s

    std::string h2b_token1st;

    h2b_token1st += hex_msb_char_to_bin(token1st[0]); // deal with msb char
    for(unsigned i = 1; i != token1st.length(); ++i)
      h2b_token1st += hex_char_to_bin(token1st[i]);

    bit_width = explicit_bits ? explicit_bits : h2b_token1st.size();
    // is_in32b = bit_width > 32 ? false : true;
    size_t dc_pos = h2b_token1st.find('?'); // dc = don't care
    if(dc_pos != std::string::npos) {
      // has_bool_dc = true;
      return process_bin_token_with_dc(g, h2b_token1st, is_signed);
    } else
      return process_bin_token(g, h2b_token1st, (uint16_t)bit_width, is_signed);
  }
  // binary
  else if(token1st[0] == '0' && token1st[1] == 'b') {
    token1st  = token1st.substr(2); // exclude leading bin 0b
    idx       = token1st.find_first_not_of('0');
    token1st  = token1st.substr(idx); // exclude leading 0s
    bit_width = explicit_bits ? explicit_bits : token1st.size();
    // is_in32b = bit_width > 32 ? false : true;

    size_t dc_pos = token1st.find('?'); // dc = don't care
    if(dc_pos != std::string::npos) {
      // has_bool_dc = true;
      return process_bin_token_with_dc(g, token1st, is_signed);
    } else
      return process_bin_token(g, token1st, (uint16_t)bit_width, is_signed);

  } else {                   // decimal
    if(token1st[2] == '?') { // case of pure question mark
      // is_pure_dc = true;
      return create_dontcare_node(g, 0);
    }

    long long   sum = 0;
    std::string d2b_token1st;
    std::string s_2scmp;

    if(token1st[0] == '-') {
      // is_explicit_signed = true;
      is_signed = true;
      // token1st = token1st[0] + token1st.substr(3);//exclude middle "0d"
      token1st = token1st.substr(3); // exclude middle "-0d"

      for(size_t i = 0; i < token1st.size(); i++) {
        sum = sum * 10 + (token1st[i] - '0');
        // fmt::print("now is round {}, token1st[{}] is {}, sum is {}\n", i, i, token1st[i], sum);
        if(sum >= 4294967296) {
          uint32_t tmp = sum - 4294967296;
          int      j   = 0;
          while(j <= 31) {
            s_2scmp = std::to_string(tmp % 2) + s_2scmp;
            tmp     = tmp >> 1;
            j++;
          }
          sum = sum >> 32;
        }
      }

      while(sum != 0) {
        s_2scmp = std::to_string(sum % 2) + s_2scmp;
        sum     = sum >> 1;
      }

      s_2scmp = '0' + s_2scmp; // add leading 0 before converting 2's complement
      // fmt::print("before 2's complement, the s_binary = {}\n", s_2scmp);

      for(size_t i = 0; i < s_2scmp.length(); i++) {
        if(s_2scmp[i] == '0')
          s_2scmp[i] = '1';
        else
          s_2scmp[i] = '0';
      }
      // fmt::print("middle 2's complement, the s_binary = {}\n", s_2scmp);

      int carry        = 0;
      int s_2scmp_size = s_2scmp.size();
      // Add all bits one by one
      for(int i = s_2scmp_size - 1; i >= 0; i--) {
        int first_bit  = s_2scmp.at(i) - '0';
        int second_bit = (i == s_2scmp_size - 1) ? 1 : 0;
        // boolean expression for sum of 3 bits
        int s = (first_bit ^ second_bit ^ carry) + '0';

        d2b_token1st = (char)s + d2b_token1st;
        // boolean expression for 3-bit addition
        carry = (first_bit & second_bit) | (second_bit & carry) | (first_bit & carry);
      }
      // fmt::print(" after 2's complement, the s_binary = {}\n", d2b_token1st);
    } else {
      token1st = token1st.substr(2);
      for(size_t i = 0; i < token1st.size(); i++) {
        sum = sum * 10 + (token1st[i] - '0');
        // fmt::print("now is round {}, token1st[{}] is {}, sum is {}\n", i, i, token1st[i], sum);
        if(sum >= 4294967296) {
          uint32_t tmp = sum - 4294967296;
          int      j   = 0;
          while(j <= 31) {
            d2b_token1st = std::to_string(tmp % 2) + d2b_token1st;
            tmp          = tmp >> 1;
            j++;
          }
          sum = sum >> 32;
        }
      }

      while(sum != 0) {
        d2b_token1st = std::to_string(sum % 2) + d2b_token1st;
        sum          = sum >> 1;
      }
    }

    bit_width = explicit_bits ? explicit_bits : d2b_token1st.size();
    // is_in32b = bit_width > 32 ? false : true;

    return process_bin_token(g, d2b_token1st, (uint16_t)bit_width, is_signed);
  }
}

uint32_t Inou_lnast_dfg::cal_bin_val_32b(const std::string &token) {
  uint16_t idx = 0;
  uint32_t val = 0;
  while(token[idx]) {
    uint8_t byte = token[idx];
    if(byte >= '0' && byte <= '1') {
      byte = byte - '0';
      val  = val << 1 | (byte & 0xF);
    }
    idx++;
  }
  return val;
}

Node Inou_lnast_dfg::process_bin_token(LGraph *g, const std::string &token, const uint16_t &bit_width, bool is_signed) {
  if(bit_width > 32) {
    std::vector<Node_pin> dpins;
    Node  node_const32;
    auto  t_size    = (uint16_t)token.size();
    Node  node_join = g->create_node();

    node_join.set_type(Join_Op);
    node_join.setup_driver_pin(0).set_bits(t_size);

    int i = 1;
    std::string token_chunk = token.substr(t_size - 32 * i, 32);

    while(t_size - 32 * i > 0) {
      node_const32 = create_const32_node(g, token_chunk, 32, is_signed);
      dpins.emplace_back(node_const32.setup_driver_pin());
      if(t_size - (32 * (i + 1)) > 0)
        token_chunk = token.substr(t_size - 32 * (i + 1), 32);
      else {
        token_chunk = token.substr(0, t_size - 32 * i);
        node_const32 = create_const32_node(g, token_chunk, token_chunk.size(), is_signed);
        dpins.emplace_back(node_const32.setup_driver_pin());
      }
      i++;
    }
    Port_ID sink_pid = 0;
    for(auto &dpin : dpins) {
      g->add_edge(dpin, node_join.setup_sink_pin(sink_pid));
      sink_pid++;
    }
    return node_join;
  } else {
    Node node_const32 = create_const32_node(g, token, bit_width, is_signed);
    return node_const32;
  }
}


Node Inou_lnast_dfg::process_bin_token_with_dc(LGraph *g, const std::string &token, bool is_signed) {
  std::vector<Node_pin> dpins;
  int  t_size = (int)token.size();
  Node node_join = g->create_node();
  node_join.set_type(Join_Op);
  node_join.setup_driver_pin().set_bits(t_size);

  std::string sdc_buf;  // continuous don't care characters
  std::string sval_buf; // continuous val characters
  int         token_size = token.size();
  for(int i = 0; i < token_size; i++) {
    if(token[i] == '?') {
      if(sval_buf.size()) {
        Node node_const32 = create_const32_node(g, sval_buf, sval_buf.size(), is_signed);
        dpins.push_back(node_const32.setup_driver_pin());
        sval_buf.clear();
      }
      sdc_buf += '?';
      if(i == token_size - 1) {
        Node node_dc = create_dontcare_node(g, sdc_buf.size());
        dpins.push_back(node_dc.setup_driver_pin());
      } else if(i + 1 < token_size && token[i + 1] != '?') {
        Node node_dc = create_dontcare_node(g, sdc_buf.size());
        dpins.push_back(node_dc.setup_driver_pin());
        sdc_buf.clear();
      }
    } else { // token[i] = some value char
      sval_buf += token[i];
      if(sval_buf.size() == 32) {
        Node node_const32 = create_const32_node(g, sval_buf, 32, is_signed);
        dpins.push_back(node_const32.setup_driver_pin());
        sval_buf = sval_buf.substr(32);
      } else if(i == token_size - 1) {
        Node node_const32 = create_const32_node(g, sval_buf, sval_buf.size(), is_signed);
        dpins.push_back(node_const32.setup_driver_pin());
      }
    }
  }

  Port_ID sink_pid = 0;
  for(auto &dpin : dpins) {
    g->add_edge(dpin, node_join.setup_sink_pin(sink_pid));
    sink_pid++;
  }
  return node_join;
}

Node Inou_lnast_dfg::create_const32_node(LGraph *g, const std::string &str_val, uint16_t node_bit_width, bool is_signed) {
  uint32_t val = cal_bin_val_32b(str_val);
  Node node_const32 = g->create_node_const(val);
  I(node_const32.get_driver_pin().get_bits() == node_bit_width);
  if(!node_const32.setup_driver_pin().has_name())
    node_const32.setup_driver_pin().set_name(absl::StrCat("0d", std::to_string(val)));

  //SH:FIXME: Attribute for Node_pin explicit/implicit bitwidth??? TBD.
  /*
  Node_bitwidth &nb = node_const32.get_driver_pin().get_bits();
  if(is_signed)
    nb.e.set_sconst(val);
  else
    nb.e.set_uconst(val);
  */
  return node_const32;
}

Node Inou_lnast_dfg::create_dontcare_node(LGraph *g, uint16_t node_bit_width ) {
  Node node_dc = g->create_node();
  node_dc.set_type(DontCare_Op);
  node_dc.setup_driver_pin().set_bits(node_bit_width);
  return node_dc;
}

std::string Inou_lnast_dfg::hex_char_to_bin(char c) {
  switch(toupper(c)) {
  case '0':
    return "0000";
  case '1':
    return "0001";
  case '2':
    return "0010";
  case '3':
    return "0011";
  case '4':
    return "0100";
  case '5':
    return "0101";
  case '6':
    return "0110";
  case '7':
    return "0111";
  case '8':
    return "1000";
  case '9':
    return "1001";
  case 'A':
    return "1010";
  case 'B':
    return "1011";
  case 'C':
    return "1100";
  case 'D':
    return "1101";
  case 'E':
    return "1110";
  case 'F':
    return "1111";
  case '?':
    return "????";
  default:
    I(false);
  }
}

std::string Inou_lnast_dfg::hex_msb_char_to_bin(char c) {
  switch(toupper(c)) {
  case '0':
    return "";
  case '1':
    return "1";
  case '2':
    return "10";
  case '3':
    return "11";
  case '4':
    return "100";
  case '5':
    return "101";
  case '6':
    return "110";
  case '7':
    return "111";
  case '8':
    return "1000";
  case '9':
    return "1001";
  case 'A':
    return "1010";
  case 'B':
    return "1011";
  case 'C':
    return "1100";
  case 'D':
    return "1101";
  case 'E':
    return "1110";
  case 'F':
    return "1111";
  case '?':
    return "????";
  default:
    I(false);
  }
}
