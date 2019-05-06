/*This file is for generating alu.dcp*/
//package com.xilinx.rapidwright.examples;
//package com.xilinx.rapidwright.examples;

import com.xilinx.rapidwright.design.Cell;
import com.xilinx.rapidwright.design.Design;
import com.xilinx.rapidwright.design.Net;
import com.xilinx.rapidwright.design.PinType;
import com.xilinx.rapidwright.design.Unisim;
import com.xilinx.rapidwright.design.tools.LUTTools;
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

public class sec_lut{

  public static void main(String[] args){
    //Create New Design for target part: PYNQ_Z1
    Design d = new Design("newDesign", Device.PYNQ_Z1);
    EDIFCell top = d.getTopEDIFCell();
    Cell lut3 = d.createAndPlaceCell("myLUT3", Unisim.LUT3, "SLICE_X0Y0/A6LUT");
    Cell lut3_2 = d.createAndPlaceCell("myLUT3_2", Unisim.LUT3, "SLICE_X1Y0/A6LUT");
    LUTTools.configureLUT(lut3, "O= (I0 & I1) | I2");
    for(EDIFPort port : lut3.getEDIFCellInst().getCellPorts())
    {
      EDIFPort topPort = top.createPort(port.getName(), port.getDirection(), 1);
      EDIFNet net = top.createNet(port.getName());
      net.createPortInst(topPort);
      net.createPortInst(port,lut3.getEDIFCellInst());
    }


    d.setAutoIOBuffers(false);
  //  d.writeCheckpoint("single_LUT_3_connected_to_top_level_ports.dcp");
  //  String sliceName = (String) opts.valueOf(SLICE_SITES_OPT);
  //  Site slice = dev.getSite(sliceName);


  EDIFPort clkPort = top.createPort("clk", EDIFDirection.INPUT, 1);
  EDIFPort cePort = top.createPort("ce", EDIFDirection.INPUT, 1);
  EDIFPort rstPort = top.createPort("rst", EDIFDirection.INPUT, 1);
  EDIFNet clk = top.createNet(clkPort.getName());
  EDIFNet rst = top.createNet(rstPort.getName());
  EDIFNet ce = top.createNet(cePort.getName());
  clk.createPortInst(clkPort);
  rst.createPortInst(rstPort);
  ce.createPortInst(cePort);
  Net clkNet = d.createNet(clk);
  Net rstNet = d.createNet(rst);
  Net ceNet = d.createNet(ce);

    Cell flop    = d.createAndPlaceCell("myflop", Unisim.FDRE, "SLICE_X1Y5/FFX");
    //clk.getLogicalNet().createPortInst("C", ff);
		//rst.getLogicalNet().createPortInst("R", ff);
		//ce.getLogicalNet().createPortInst("CE", ff);

    d.writeCheckpoint("LUT3_FF.dcp");



  }
}


    //Cell or2 = d.createAndPlaceCell("or2", Unisim.OR2, "SLICE_X100Y100/A6LUT");
    //Cell and2 = d.createAndPlaceCell("and2", Unisim.AND2, "SLICE_X100Y100/A6LUT");
/*
    d.setAutoIOBuffers(true);
    Cell b0 = d.createAndPlaceIOB("button0", PinType.IN , "D19",  "LVCMOS33");
    Cell b1 = d.createAndPlaceIOB("button1", PinType.IN , "D18",  "LVCMOS33");
    Cell out0    = d.createAndPlaceIOB("out0"   , PinType.OUT, "R14",  "LVCMOS33");

    //Cell flop    = d.createAndPlaceCell(null, "myflop"   , Unisim.FDRE);

    //inputNet.createPortInst("D", flop);
    //outputNet.createPortInst("Q", flop);
    //
    //	private static void connectFDRECtrl(Net clk, Net rst, Net ce, Cell ff){
		// clk.getLogicalNet().createPortInst("C", ff);
		// rst.getLogicalNet().createPortInst("R", ff);
		// ce.getLogicalNet().createPortInst("CE", ff);
    //
    // https://www.xilinx.com/support/documentation/sw_manuals/xilinx2018_3/ug953-vivado-7series-libraries.pdf


    // Connect Button 0 to the LUT2 input I0
        Net net0 = d.createNet("button0_IBUF");
        net0.connect(b0, "O");
        net0.connect(lut3, "I0");
        //net0.connect(b0, "O");
        //net0.connect(or2, "I1");

        // Connect Button 1 to the LUT2 input I1
        Net net1 = d.createNet("button1_IBUF");
        net1.connect(b1, "O");
        net1.connect(lut3, "I1");
        net1.connect(b1, "O");
        net1.connect(lut3, "I2");

    // Connect the LUT2 (AND2) to the LED IO
        Net net2 = d.createNet("or2");
        net2.connect(lut3, "O");
        net2.connect(out0, "I");

        d.routeSites();

        new Router(d).routeDesign();

        d.writeCheckpoint("or_and_logic.dcp");
*/
