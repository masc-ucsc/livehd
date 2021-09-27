/* cgen_memory_generate.cpp
 * Script written for use with MASC LiveHD/Pyrope
 * Given a number of read ports and write ports, automatically generates
 * Verilog memory module with forwarding/latency/masking parameters.
 */

#include <fstream>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

using namespace std;

int main(int argc, char *argv[]) {
  int read_ports, write_ports;
  int i, j;

  std::vector<std::string> args(argv, argv + argc);

  if (args.size() != 5) {
    cout << "Please specify amount of read/write ports" << endl;
    cout << "Ex: ./cgen_memory_generate read 4 write 2" << endl;
    exit(EXIT_FAILURE);
  }

  if ((args[1] == "read") && ((read_ports = std::stoi(args[2])) > 0) &&
      (args[3] == "write") && ((write_ports = std::stoi(args[4])) > 0)) {

    // Generate Verilog memory module
    std::string filename = "cgen_memory_" + std::to_string(read_ports) + "rd_" +
                           std::to_string(write_ports) + "wr";
    std::ofstream outfile(filename + ".v");
    outfile
        << "`define log2(n)   ((n) <= (1<<0) ? 0 : (n) <= (1<<1) ? 1 :\\\n"
        << "                   (n) <= (1<<2) ? 2 : (n) <= (1<<3) ? 3 :\\\n"
        << "                   (n) <= (1<<4) ? 4 : (n) <= (1<<5) ? 5 :\\\n"
        << "                   (n) <= (1<<6) ? 6 : (n) <= (1<<7) ? 7 :\\\n"
        << "                   (n) <= (1<<8) ? 8 : (n) <= (1<<9) ? 9 :\\\n"
        << "                   (n) <= (1<<10) ? 10 : (n) <= (1<<11) ? 11 :\\\n"
        << "                   (n) <= (1<<12) ? 12 : (n) <= (1<<13) ? 13 :\\\n"
        << "                   (n) <= (1<<14) ? 14 : (n) <= (1<<15) ? 15 :\\\n"
        << "                   (n) <= (1<<16) ? 16 : (n) <= (1<<17) ? 17 :\\\n"
        << "                   (n) <= (1<<18) ? 18 : (n) <= (1<<19) ? 19 :\\\n"
        << "                   (n) <= (1<<20) ? 20 : (n) <= (1<<21) ? 21 :\\\n"
        << "                   (n) <= (1<<22) ? 22 : (n) <= (1<<23) ? 23 :\\\n"
        << "                   (n) <= (1<<24) ? 24 : (n) <= (1<<25) ? 25 :\\\n"
        << "                   (n) <= (1<<26) ? 26 : (n) <= (1<<27) ? 27 :\\\n"
        << "                   (n) <= (1<<28) ? 28 : (n) <= (1<<29) ? 29 :\\\n"
        << "                   (n) <= (1<<30) ? 30 : (n) <= (1<<31) ? 31 : "
           "32)\n"
        << std::endl;

    outfile
        << "module " + filename + "\n"
        << "  #(parameter BITS = 4, SIZE=128, FWD=1, LATENCY_0=1, WENSIZE=1)\n"
        << "    (input clock\n"
        << std::endl;

    for (i = 0; i < read_ports; i++) {
      outfile << "     ,input [`log2(SIZE)-1:0]  rd_addr_" << i << "\n"
              << "     ,input                    rd_enable_" << i << "\n"
              << "     ,output reg [BITS-1:0]    rd_dout_" << i << "\n"
              << std::endl;
    }

    for (i = 0; i < write_ports; i++) {
      outfile << "     ,input [`log2(SIZE)-1:0]  wr_addr_" << i << "\n"
              << "     ,input [WENSIZE-1:0]      wr_enable_" << i << "\n"
              << "     ,input [BITS-1:0]         wr_din_" << i << "\n"
              << std::endl;
    }

    outfile << ");\n" << std::endl;

    outfile << "localparam MASKSIZE = BITS/WENSIZE;\n" << std::endl;

    for (i = 0; i < read_ports; i++) {
      outfile << "reg [BITS-1:0]        d" << i << "_mem;\n" << std::endl;
    }

    outfile << "generate\n"
            << "    reg [BITS-1:0]        data[SIZE-1:0];\n"
            << "    integer i;\n"
            << "    always @(posedge clock) begin\n"
            << "      for(i=0;i<WENSIZE;i=i+1) begin" << std::endl;

    for (i = 0; i < write_ports; i++) {
      outfile << "        if(wr_enable_" << i << "[i]) begin\n"
              << "            data[wr_addr_" << i
              << "][i*MASKSIZE +: MASKSIZE] <=\n"
              << "              wr_din_" << i << "[i*MASKSIZE +: MASKSIZE];\n"
              << "        end" << std::endl;
    }

    outfile << "      end\n"
            << "    end\n"
            << std::endl;

    outfile << "    always @(posedge clock) begin" << std::endl;

    for (i = 0; i < read_ports; i++) {
      outfile << "      if (rd_enable_" << i << ")\n"
              << "        d" << i << "_mem <= data[rd_addr_" << i << "];\n"
              << "      else\n"
              << "        d" << i << "_mem <= {BITS{1'bx}};" << std::endl;
    }

    outfile << "    end\n"
            << "endgenerate\n"
            << std::endl;

    for (i = 0; i < read_ports; i++) {
      outfile << "reg [BITS-1:0]        d" << i << "_fwd;\n" << std::endl;
    }

    outfile << "generate\n"
            << "  if (FWD) begin:BLOCK_FWD_TRUE" << std::endl;

    for (i = 0; i < read_ports; i++) {
      for (j = 0; j < write_ports; j++) {
        outfile << "    reg [WENSIZE-1:0] fwd_decision_cmp_" << i << "rd_" << j
                << "wr;" << std::endl;
      }
    }

    outfile << "    genvar j;\n"
            << "    for(j=0;j<WENSIZE;j=j+1) begin:FWD_BLOCK_CALC_0\n"
            << "    always_comb begin" << std::endl;

    for (i = 0; i < read_ports; i++) {
      for (j = 0; j < write_ports; j++) {
        outfile << "      fwd_decision_cmp_" << i << "rd_" << j
                << "wr[j] = rd_addr_" << i << " == wr_addr_" << j << ";"
                << std::endl;
      }
    }

    for (i = 0; i < read_ports; i++) {
      outfile << "      d" << i
              << "_fwd[j*MASKSIZE +: MASKSIZE] = " << std::endl;
      for (j = 0; j < write_ports; j++) {
        outfile << "      wr_enable_" << j << "[j] && fwd_decision_cmp_" << i
                << "rd_" << j << "wr[j]?\n"
                << "      wr_din_" << j
                << "[j*MASKSIZE +: MASKSIZE]:" << std::endl;
      }
      outfile << "      d" << i << "_mem[j*MASKSIZE +: MASKSIZE];\n"
              << std::endl;
    }

    outfile << "      end\n"
            << "    end\n"
            << "  end else begin:BLOCK_FWD_FALSE\n"
            << "    always_comb begin" << std::endl;

    for (i = 0; i < read_ports; i++) {
      outfile << "      d" << i << "_fwd = d" << i << "_mem;" << std::endl;
    }

    outfile << "    end\n"
            << "  end\n"
            << "endgenerate\n"
            << std::endl;

    outfile << "generate\n"
            << "	if (LATENCY_0==1) begin:BLOCK1\n"
            << "    always @(posedge clock) begin" << std::endl;

    for (i = 0; i < read_ports; i++) {
      outfile << "      rd_dout_" << i << " <= d" << i << "_fwd;" << std::endl;
    }

    outfile << "    end\n"
            << "  end else begin:BLOCK2" << std::endl;

    for (i = 0; i < read_ports; i++) {
      outfile << "    assign rd_dout_" << i << " = d" << i << "_fwd;"
              << std::endl;
    }

    outfile << "  end\n"
            << "endgenerate\n"
            << "endmodule" << std::endl;

    outfile.close();
  } else {
    cout << "invalid input" << endl;
  }
}