module \mem_multidim_rw.md (
  input            clock,
  input      [7:0] a,
  input            wi,
  input      [1:0] wj,
  input            ri,
  input      [1:0] rj,
  input            we,
  output     [7:0] z
);

  reg [7:0] data [0:7];

  wire [2:0] waddr = {wi, wj};  // row-major: wi*4 + wj
  wire [2:0] raddr = {ri, rj};

  always @(posedge clock) begin
    if (we) data[waddr] <= a;
  end

  // async read + same-cycle write forwarding (fwd=1 for reg memories)
  assign z = (we && (waddr == raddr)) ? a : data[raddr];

endmodule
