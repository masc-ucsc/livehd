module top(input [7:0] a, b, output [3:0] x, y);
  function automatic [3:0] prefix_count(input [7:0] valid);
    integer lane;
    begin
      prefix_count = 0;
      for (lane = 0; lane < 8; lane = lane + 1)
        if (valid[0] && valid[lane]) prefix_count = lane + 1;
    end
  endfunction
  assign x = prefix_count(a);
  assign y = prefix_count(b);
endmodule
