`timescale 1ns / 1ps

//define symbol by given encoding style
`define A 2'b00
`define B 2'b01
`define C 2'b10
`define D 2'b11


module regex(
    input clk, 
    input res_n, 
    input [1:0] symbol_in,
    input last_symbol, 
    output reg result, 
    output reg done
    );
    
    
    //State Encode by One-hot
    parameter  [8:0]  IDLE      = 9'b000000001;
    parameter  [8:0]  MATCH     = 9'b000000010;
    parameter  [8:0]  NOT_MATCH = 9'b000000100;
    parameter  [8:0]  FIRST_A   = 9'b000001000;
    parameter  [8:0]  SECOND_B  = 9'b000010000;
    parameter  [8:0]  THIRD_C   = 9'b000100000;
    parameter  [8:0]  FOURTH_A  = 9'b001000000;
    parameter  [8:0]  FOURTH_D  = 9'b010000000;
    parameter  [8:0]  FIFTH_BCD = 9'b100000000;

    reg [8:0] state, next_state;
    
    //Combinatorial Block
    always @ (*)
    begin  
      
      case(state)
                
        IDLE :  if ((symbol_in==`D)&(last_symbol)) 
                begin
                    next_state <= MATCH;
                end 
                
                else if ((symbol_in==`A)&(~last_symbol)) 
                begin
                    next_state <= FIRST_A;
                end 
                
                else if (((symbol_in==`A)&(last_symbol))|
                          (symbol_in==`B)|
                          (symbol_in==`C)|
                         ((symbol_in==`D)&(~last_symbol))) 
                begin
                    next_state <= NOT_MATCH;
                end 
           
                else 
                begin
                    next_state <= IDLE;
                end	         
                		
        MATCH : next_state <= IDLE;	
        NOT_MATCH : next_state <= IDLE;
             
        FIRST_A : if ((symbol_in==`C)&(~last_symbol)) 
                begin
                    next_state <= THIRD_C;
                end 
                
                else if ((symbol_in==`B)&(~last_symbol)) 
                begin
                    next_state <= SECOND_B;
                end 
                
                else if ((symbol_in==`A)|
                        ((symbol_in==`B)&(last_symbol))|
                        ((symbol_in==`C)&(last_symbol))|
                         (symbol_in==`D)) 
                begin
                    next_state <= NOT_MATCH;
                end 
           
                else 
                begin
                    next_state <= IDLE;
                end
                
        SECOND_B : if ((symbol_in==`B)&(~last_symbol)) 
                begin
                    next_state <= SECOND_B;
                end 
                
                else if ((symbol_in==`C)&(~last_symbol)) 
                begin
                    next_state <= THIRD_C;
                end 
                
                else if ((symbol_in==`A)|
                        ((symbol_in==`B)&(last_symbol))|
                        ((symbol_in==`C)&(last_symbol))|
                         (symbol_in==`D)) 
                begin
                    next_state <= NOT_MATCH;
                end 
           
                else 
                begin
                    next_state <= IDLE;
                end
                
        THIRD_C : if ((symbol_in==`A)&(~last_symbol)) 
                begin
                    next_state <= FOURTH_A;
                end 
                
                else if ((symbol_in==`D)&(~last_symbol)) 
                begin
                    next_state <= FOURTH_D;
                end 
                
                else if (((symbol_in==`A)&(last_symbol))|
                          (symbol_in==`B)|
                          (symbol_in==`C)|
                         ((symbol_in==`D)&(last_symbol))) 
                begin
                    next_state <= NOT_MATCH;
                end 
           
                else 
                begin
                    next_state <= IDLE;
                end
                
        FOURTH_A : if ((symbol_in==`A)&(~last_symbol)) 
                begin
                    next_state <= FIRST_A;
                end 
                
                else if ((symbol_in==`D)&(last_symbol)) 
                begin
                    next_state <= MATCH;
                end 
                
                else if (((symbol_in==`A)&(last_symbol))|
                          (symbol_in==`B)|
                          (symbol_in==`C)|
                         ((symbol_in==`D)&(~last_symbol))) 
                begin
                    next_state <= NOT_MATCH;
                end 
           
                else 
                begin
                    next_state <= IDLE;
                end
                
        FOURTH_D : if (((symbol_in==`B)&(~last_symbol))|
                       ((symbol_in==`C)&(~last_symbol))|
                       ((symbol_in==`D)&(~last_symbol))) 
                begin
                    next_state <= FIFTH_BCD;
                end 
                
                else if ((symbol_in==`A)|
                        ((symbol_in==`B)&(last_symbol))|
                        ((symbol_in==`C)&(last_symbol))|
                        ((symbol_in==`D)&(last_symbol))) 
                begin
                    next_state <= NOT_MATCH;
                end 
           
                else 
                begin
                    next_state <= IDLE;
                end
               
        FIFTH_BCD : if ((symbol_in==`D)&(last_symbol)) 
                begin
                    next_state <= MATCH;
                end 
                
                else if ((symbol_in==`A)&(~last_symbol)) 
                begin
                    next_state <= FIRST_A;
                end 
                
                else if (((symbol_in==`A)&(last_symbol))|
                          (symbol_in==`B)|
                          (symbol_in==`C)|
                         ((symbol_in==`D)&(!last_symbol))) 
                begin
                    next_state <= NOT_MATCH;
                end 
           
                else 
                begin
                    next_state <= IDLE;
                end
       default : next_state <= IDLE;
      endcase
    end
    
    
    //Sequential block
    always @ (posedge clk)
    begin 
      if (!res_n) 
      begin
        result <=  1'b0;
        done <=  1'b0;
        state <=  IDLE;
      end 
      
      else 
      begin
        state <=  next_state;
        case(next_state)
        
        IDLE : 
            begin
                result <=  1'b0;
                done <=  1'b0;
            end
        MATCH : 
            begin
                
                result <=  1'b1;
                done <=  1'b1;
            end
            
        NOT_MATCH : 
            begin
                result <=  1'b0;
                done <=  1'b1;
            end
            
        FIRST_A : 
            begin
                result <=  1'b0;
                done <=  1'b0;
            end
            
        SECOND_B : 
            begin
                result <=  1'b0;
                done <=  1'b0;
            end
          
        THIRD_C : 
            begin
                result <=  1'b0;
                done <=  1'b0;
            end
            
        FOURTH_A : 
            begin
                result <=  1'b0;
                done <=  1'b0;
            end
            
        FOURTH_D : 
            begin
                result <=  1'b0;
                done <=  1'b0;
            end
            
        FIFTH_BCD : 
            begin
                result <=  1'b0;
                done <=  1'b0;
            end
            
        default : 
            begin
                state <=  IDLE;
            end
            
        endcase
      end
    end         
endmodule
