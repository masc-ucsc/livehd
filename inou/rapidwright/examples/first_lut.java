/*This file is for generating alu.dcp*/
//package com.xilinx.rapidwright.examples;
//package com.xilinx.rapidwright.examples;

import com.xilinx.rapidwright.design.Cell;
import com.xilinx.rapidwright.design.Design;
import com.xilinx.rapidwright.design.Net;
import com.xilinx.rapidwright.design.PinType;
import com.xilinx.rapidwright.design.Unisim;
import com.xilinx.rapidwright.device.Device;
import com.xilinx.rapidwright.router.Router;
import com.xilinx.rapidwright.util.FileTools;
import com.xilinx.rapidwright.util.MessageGenerator;
import com.xilinx.rapidwright.edif.EDIFHierNet;
import com.xilinx.rapidwright.edif.EDIFCell;
import com.xilinx.rapidwright.edif.EDIFCellInst;
import com.xilinx.rapidwright.edif.EDIFDirection;
import com.xilinx.rapidwright.edif.EDIFNet;
import com.xilinx.rapidwright.edif.EDIFNetlist;
import com.xilinx.rapidwright.edif.EDIFPort;
import com.xilinx.rapidwright.edif.EDIFPortInst;
import com.xilinx.rapidwright.edif.EDIFTools;
import com.xilinx.rapidwright.placer.blockplacer.Point;
import com.xilinx.rapidwright.placer.blockplacer.SmallestEnclosingCircle;
import com.xilinx.rapidwright.router.RouteNode;
import com.xilinx.rapidwright.router.Router;
import com.xilinx.rapidwright.edif.EDIFParser;
import com.xilinx.rapidwright.edif.EDIFTools;

import java.io.File;
import java.util.*;
import java.io.FileNotFoundException;
import java.io.PrintWriter;
import java.util.LinkedList;
import java.util.Queue;

public class first_lut{
  public static void main(String[] args){
    //Create New Design for target part: PYNQ_Z1
    Design d = new Design("newDesign", Device.PYNQ_Z1); 
    
    Cell or2 = d.createAndPlaceCell("or2", Unisim.OR2, "SLICE_X100Y100/A6LUT");
    Cell b0 = d.createAndPlaceIOB("button0", PinType.IN , "D19",  "LVCMOS33");
    Cell b1 = d.createAndPlaceIOB("button1", PinType.IN , "D20",  "LVCMOS33");
    Cell out0    = d.createAndPlaceIOB("out0"   , PinType.OUT, "R14",  "LVCMOS33");
						
// Connect Button 0 to the LUT2 input I0
    Net net0 = d.createNet("button0_IBUF");
    net0.connect(b0, "O");
    net0.connect(or2, "I0");

    // Connect Button 1 to the LUT2 input I1
    Net net1 = d.createNet("button1_IBUF");
    net1.connect(b1, "O");
    net1.connect(or2, "I1");
 																		
// Connect the LUT2 (AND2) to the LED IO
    Net net2 = d.createNet("or2");
    net2.connect(or2, "O");
    net2.connect(out0, "I");
    
    d.routeSites();

    new Router(d).routeDesign();

    d.writeCheckpoint("or_logic.dcp");


  }
}

