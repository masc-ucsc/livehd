#include "pass_dfg.hpp"
#include <string>

//Sheng Zone
void Pass_dfg::test_const_conversion() {
  LGraph *tg = new LGraph(opack.lgdb, opack.graph_name, false);

  //const std::string str_in = "0d?";//24 bits
  //const std::string str_in = "0x00FF?FF_FF?_u";//24 bits
  //const std::string str_in = "0b1111u";//4 bits
  //const std::string str_in = "0xFFFF_FF_s";//24 bits
  //const std::string str_in = "0x7AFF_FFFF_FFFF_FFFF_FF_s";//40 bits
  //const std::string str_in = "0b1111_1111_1111_1111_1111_1111_1111_1111_1111";
  //const std::string str_in = "0b00011111111_11111111_11111111s";//legal but logic conflict declaration
  //const std::string str_in = "-0d2147483647";
  //const std::string str_in = "-0d128";
  const std::string str_in = "-0d4294967297";
  //const std::string str_in = "-0d2147483648";
  //const std::string str_in = "-0d2147483649";
  //const std::string str_in = "-0d5294967298";
  //const std::string str_in = "0b11101111_11101111_11101111_11101111_11101111???110?10";
  //const std::string str_in = "0d2147483647";
  //const std::string str_in = "-0d8";

  bool              is_signed          = false;
  bool              is_in32b           = true;
  bool              is_explicit_signed = false; //explicit assign the signed mark
  bool              has_bool_dc        = false; //dc = don't care, ex: 0x100??101
  bool              is_pure_dc         = false; //ex: ????
  uint32_t          val                = 0;
  uint32_t          explicit_bits      = 0;
  size_t            bit_width          = 0;

  resolve_constant(tg, str_in, is_signed, is_in32b, is_explicit_signed, has_bool_dc, is_pure_dc, val, explicit_bits, bit_width);

  fmt::print("\n");
  fmt::print("out of 32 bits range?      {}\n",!is_in32b);
  fmt::print("signed:                    {}\n",is_signed);
  fmt::print("explicit sign declaration: {}\n",is_explicit_signed);
  fmt::print("has boolean don't care:    {}\n",has_bool_dc);
  fmt::print("stored value:              {}\n",val);
  fmt::print("explicit_bits:             {}\n",explicit_bits);
  fmt::print("bit_width:                 {}\n",bit_width);
  fmt::print("\n");
  tg->sync();
}


Index_ID Pass_dfg::resolve_constant(LGraph *g, const std::string& str, bool& is_signed, bool& is_in32b,
                                    bool& is_explicit_signed, bool& has_bool_dc, bool& is_pure_dc, uint32_t& val,
                                    uint32_t& explicit_bits, size_t& bit_width)
{
  std::string token1st, token2nd;
  std::string str_in = str;
  char rm = '_';//remove '_' in 0xF___FFFF
  str_in.erase(std::remove(str_in.begin(), str_in.end(),rm), str_in.end());
  size_t s_pos = str_in.find('s');//O(n)
  size_t u_pos = 0;
  size_t idx;

  //extract 1st and 2nd tokens, delimiter: s or u
  if(s_pos != std::string::npos){
    token1st = str_in.substr(0,s_pos);
    token2nd = str_in.substr(s_pos+1);
    is_signed = true;
    is_explicit_signed = true;
  }
  else{
    u_pos = str_in.find('u');//O(n)
    if(u_pos != std::string::npos){
      token1st = str_in.substr(0,u_pos);
      token2nd = str_in.substr(u_pos+1);
      is_explicit_signed = true;
    }
    else
      token1st = str_in;
  }

  //explicit bits width
  if(token2nd != "") {
    fmt::print("{}::::::\n", token2nd);
    explicit_bits = (uint32_t) std::stoi(token2nd);
  }
  else
    explicit_bits = 0;

  //main process for bin/hex/dec
  //hex
  if(token1st[0] == '0' && token1st[1] == 'x'){
    token1st = token1st.substr(2);//exclude leading hex 0x
    idx = token1st.find_first_not_of('0') ;
    token1st = token1st.substr(idx);//exclude leading 0s

    std::string h2b_token1st;

    h2b_token1st += hex_msb_char_to_bin(token1st[0]); //deal with msb char
    for(unsigned i = 1; i != token1st.length(); ++i)
      h2b_token1st += hex_char_to_bin(token1st[i]);

    bit_width = explicit_bits ? explicit_bits : h2b_token1st.size();
    is_in32b = bit_width > 32 ? false : true;
    size_t dc_pos = h2b_token1st.find('?'); //dc = don't care
    if(dc_pos != std::string::npos){
      has_bool_dc = true;
      return process_bin_token_with_dc(g, h2b_token1st);
    }
    else
      return process_bin_token(g, h2b_token1st, (uint16_t)bit_width, val);
  }
  //binary
  else if (token1st[0] == '0' && token1st[1] == 'b') {
    token1st = token1st.substr(2);//exclude leading bin 0b
    idx = token1st.find_first_not_of('0');
    token1st = token1st.substr(idx);//exclude leading 0s
    bit_width = explicit_bits ? explicit_bits : token1st.size();
    is_in32b = bit_width > 32 ? false : true;

    size_t dc_pos = token1st.find('?'); //dc = don't care
    if(dc_pos != std::string::npos){
      has_bool_dc = true;
      return process_bin_token_with_dc(g, token1st);
    }
    else
      return process_bin_token(g, token1st, (uint16_t)bit_width, val);

  }
  else{//decimal
    if(token1st[2] == '?') {//case of pure question mark
      is_pure_dc = true;
      return create_dontcare_node(g,0);
    }

    long long sum = 0;
    std::string d2b_token1st;
    std::string s_2scmp;

    if(token1st[0] == '-'){
      is_explicit_signed = true;
      is_signed = true;
      //token1st = token1st[0] + token1st.substr(3);//exclude middle "0d"
      token1st = token1st.substr(3);//exclude middle "-0d"

      for(size_t i = 0; i < token1st.size(); i++){
        sum = sum *10 + (token1st[i] - '0');
        fmt::print("now is round {}, token1st[{}] is {}, sum is {}\n", i, i, token1st[i], sum);
        if(sum >= 4294967296){
          uint32_t tmp = sum- 4294967296;
          int j = 0;
          while(j <= 31){
            s_2scmp = std::to_string(tmp%2) + s_2scmp;
            tmp = tmp >> 1;
            j ++;
          }
          sum = sum >> 32;
        }
      }

      while(sum != 0){
        s_2scmp = std::to_string(sum%2) + s_2scmp;
        sum = sum >> 1;
      }

      s_2scmp = '0' + s_2scmp; //add leading 0 before converting 2's complement
      fmt::print("before 2's complement, the s_binary = {}\n", s_2scmp);

      for(size_t i = 0; i< s_2scmp.length(); i++){
        if(s_2scmp[i] == '0')
          s_2scmp[i] = '1';
        else
          s_2scmp[i] = '0';
      }
      fmt::print("middle 2's complement, the s_binary = {}\n", s_2scmp);

      int carry = 0;
      int s_2scmp_size = s_2scmp.size();
      // Add all bits one by one
      for (int i = s_2scmp_size-1 ; i >= 0 ; i--)
      {
        int first_bit = s_2scmp.at(i) - '0';
        int second_bit = (i==s_2scmp_size-1) ? 1 : 0;
        // boolean expression for sum of 3 bits
        int s = (first_bit ^ second_bit ^ carry)+'0';

        d2b_token1st = (char)s + d2b_token1st;
        // boolean expression for 3-bit addition
        carry = (first_bit & second_bit) | (second_bit & carry) | (first_bit & carry);
      }
      fmt::print(" after 2's complement, the s_binary = {}\n", d2b_token1st);
    }
    else{
      token1st = token1st.substr(2);
      for(size_t i = 0; i < token1st.size(); i++){
        sum = sum *10 + (token1st[i] - '0');
        fmt::print("now is round {}, token1st[{}] is {}, sum is {}\n", i, i, token1st[i], sum);
        if(sum >= 4294967296){
          uint32_t tmp = sum- 4294967296;
          int j = 0;
          while(j <= 31){
            d2b_token1st = std::to_string(tmp%2) + d2b_token1st;
            tmp = tmp >> 1;
            j ++;
          }
          sum = sum >> 32;
        }
      }

      while(sum != 0){
        d2b_token1st = std::to_string(sum%2) + d2b_token1st;
        sum = sum >> 1;
      }
    }

    bit_width = explicit_bits ? explicit_bits : d2b_token1st.size();
    is_in32b = bit_width > 32 ? false : true;

    return process_bin_token(g, d2b_token1st, (uint16_t)bit_width, val);

  }
}


uint32_t Pass_dfg::cal_bin_val_32b(const std::string& token){
  uint16_t idx = 0;
  uint32_t val = 0;
  while(token[idx]){
    uint8_t byte = token[idx];
    if(byte >= '0' && byte <= '1'){
      byte = byte - '0';
      val = val << 1 | (byte & 0xF);
    }
    idx++;
  }
  return val;
}


Index_ID Pass_dfg::process_bin_token (LGraph *g, const std::string& token, const uint16_t & bit_width, uint32_t& val){
  if(bit_width > 32){
    std::vector<Node_Pin> inp_pins;
    Index_ID nid_const32;
    int t_size = (int)token.size();
    Index_ID nid_join = g->create_node().get_nid();
    g->node_type_set(nid_join, Join_Op);
    g->set_bits(nid_join, t_size);

    int i = 1;
    std::string token_chunk = token.substr(t_size-32*i,32);

    while(t_size-32*i > 0){
      fmt::print("@round{}, token_chunk:                  {}\n", i, token_chunk);
      nid_const32 = create_const32_node(g, token_chunk, 32, val);
      inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      if(t_size-(32*(i+1)) > 0)
        token_chunk = token.substr(t_size - 32*(i+1),32);
      else{
        token_chunk = token.substr(0,t_size-32*i);
        fmt::print("@round{}, token_chunk:                 {}\n", i+1, token_chunk);
        nid_const32 = create_const32_node(g, token_chunk, token_chunk.size(), val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      }
      i++;
    }
    int pid = 0;
    for(auto &inp_pin : inp_pins) {
      g->add_edge(inp_pin, Node_Pin(nid_join, pid, true));
      pid++;
    }
    val = 0;// val = 0 for values that are out of 32bit range
    return nid_join;
  }
  else{
    Index_ID nid_const32 = create_const32_node(g, token, bit_width, val);
    return nid_const32;
  }
}

Index_ID Pass_dfg::process_bin_token_with_dc (LGraph *g, const std::string& token){
  fmt::print("process binary with don't cares!\n");
  uint32_t val;
  std::vector<Node_Pin> inp_pins;
  int t_size = (int)token.size();
  Index_ID nid_join = g->create_node().get_nid();
  g->node_type_set(nid_join, Join_Op);
  g->set_bits(nid_join, t_size);

  std::string sdc_buf; //continuous don't care characters
  std::string sval_buf;//continuous val characters
  int token_size = token.size();
  for(int i = 0; i < token_size; i++){
    if(token[i] == '?'){
      if(sval_buf.size()){
        Index_ID nid_const32 = create_const32_node(g, sval_buf, sval_buf.size(), val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
        sval_buf.clear();
      }
      sdc_buf += '?';
      if(i == token_size-1){
        Index_ID nid_dc = create_dontcare_node(g,sdc_buf.size());
        inp_pins.push_back(Node_Pin(nid_dc, 0, false));
      }
      else if(i+1 < token_size && token[i+1]!= '?'){
        Index_ID nid_dc = create_dontcare_node(g,sdc_buf.size());
        inp_pins.push_back(Node_Pin(nid_dc, 0, false));
        sdc_buf.clear();
      }
    }
    else{// token[i] = some value char
      sval_buf += token[i];
      if(sval_buf.size() == 32){
        Index_ID nid_const32 = create_const32_node(g, sval_buf, 32, val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
        sval_buf = sval_buf.substr(32);
      }
      else if(i == token_size -1){
        Index_ID nid_const32 = create_const32_node(g, sval_buf, sval_buf.size(), val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      }
    }
  }

  int pid = 0;
  for(auto &inp_pin : inp_pins) {
    g->add_edge(inp_pin, Node_Pin(nid_join, pid, true));
    pid++;
  }
  return nid_join;
}

Index_ID Pass_dfg::create_const32_node (LGraph *g, const std::string& val_str, uint16_t node_bit_width, uint32_t& val){
    val = cal_bin_val_32b(val_str);

  Index_ID nid_const32 = g->create_node().get_nid();
  g->node_u32type_set(nid_const32,val);
  g->set_bits(nid_const32, node_bit_width);
  return nid_const32;
}

Index_ID Pass_dfg::create_dontcare_node (LGraph *g, uint16_t node_bit_width ){
  Index_ID nid_dc = g->create_node().get_nid();
  g->node_type_set(nid_dc, DontCare_Op);
  g->set_bits(nid_dc, node_bit_width);
  return nid_dc;
}

std::string Pass_dfg::hex_char_to_bin(char c) {
  switch(toupper(c)) {
    case '0': return "0000"; case '1': return "0001"; case '2': return "0010"; case '3': return "0011";
    case '4': return "0100"; case '5': return "0101"; case '6': return "0110"; case '7': return "0111";
    case '8': return "1000"; case '9': return "1001"; case 'A': return "1010"; case 'B': return "1011";
    case 'C': return "1100"; case 'D': return "1101"; case 'E': return "1110"; case 'F': return "1111";
    case '?': return "????"; default : assert(false);
  }
}

std::string Pass_dfg::hex_msb_char_to_bin(char c) {
  switch(toupper(c)) {
    case '0': return "";     case '1': return "1";    case '2': return "10";   case '3': return "11";
    case '4': return "100";  case '5': return "101";  case '6': return "110";  case '7': return "111";
    case '8': return "1000"; case '9': return "1001"; case 'A': return "1010"; case 'B': return "1011";
    case 'C': return "1100"; case 'D': return "1101"; case 'E': return "1110"; case 'F': return "1111";
    case '?': return "????"; default : assert(false);
  }
}

void Pass_dfg_options::set(const py::dict &dict) {
  for (auto item : dict) {
    const auto &key = item.first.cast<std::string>();

    try {
      if (is_opt(key,"src") ) {
        const auto &val = item.second.cast<std::string>();
        src = val;
      }else{
        set_val(key,item.second);
      }
    } catch (const std::invalid_argument& ia) {
      fmt::print("ERROR: key {} has an invalid argument {}\n",key);
    }
  }

  console->warn("pass_dfg src:{} lgdb:{} graph_name:{}"
      ,src,lgdb, graph_name);
}
