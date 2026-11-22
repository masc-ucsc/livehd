module clock_phase(input clk, input data, output q, output qinv);
  wire master, half, quarter, n, d1, d2;
  BUF buffer(.A(clk), .Y(master));
  FF div1(.CLK(master), .D(d1), .Q(half));
  INV fb1(.A(half), .Y(d1));
  FF div2(.CLK(half), .D(d2), .Q(quarter));
  INV fb2(.A(quarter), .Y(d2));
  FF capture(.CLK(quarter), .D(data), .Q(q));
  INV flip(.A(clk), .Y(n));
  FF capture_inv(.CLK(n), .D(data), .Q(qinv));
endmodule
