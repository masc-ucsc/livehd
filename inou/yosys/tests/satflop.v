module satflop (
data0   ,
data1   ,		
clk    , 
reset  , 
q        
);

input data0,data1, clk, reset ; 


output q;


reg q;


always @ ( posedge clk or posedge reset)
begin
if (reset) 
  q <= data0+data1;
          
  else  
    q<=data1;
   
 end
endmodule 
