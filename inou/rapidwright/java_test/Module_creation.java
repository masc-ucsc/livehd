/*This file is for generating alu.dcp*/
//package com.xilinx.rapidwright.examples;
//package com.xilinx.rapidwright.examples;

import com.xilinx.rapidwright.design.Cell;
import com.xilinx.rapidwright.design.Design;
import com.xilinx.rapidwright.design.DesignTools;
import com.xilinx.rapidwright.design.Module;
import com.xilinx.rapidwright.design.ModuleInst;
import com.xilinx.rapidwright.design.Net;
import com.xilinx.rapidwright.design.PinType;
import com.xilinx.rapidwright.design.Port;
import com.xilinx.rapidwright.design.SitePinInst;
import com.xilinx.rapidwright.design.Unisim;
import com.xilinx.rapidwright.device.Device;
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
import com.xilinx.rapidwright.edif.EDIFLibrary;
import com.xilinx.rapidwright.placer.blockplacer.BlockPlacer;
import com.xilinx.rapidwright.placer.blockplacer.BlockPlacer2;
import com.xilinx.rapidwright.placer.blockplacer.Point;
import com.xilinx.rapidwright.placer.blockplacer.SmallestEnclosingCircle;
import com.xilinx.rapidwright.device.PIP;
import com.xilinx.rapidwright.router.Router;
import com.xilinx.rapidwright.router.RouteNode;
import com.xilinx.rapidwright.router.Router;
import com.xilinx.rapidwright.edif.EDIFParser;
import com.xilinx.rapidwright.edif.EDIFTools;
import java.io.File;
import java.util.*;
import java.io.FileNotFoundException;
import java.io.PrintWriter;



public class Module_creation{

  private static void connectModuleInstPin(Net net, String portName, ModuleInst modInst) {
    SitePinInst pin = modInst.getCorrespondingPin(modInst.getModule().getPort(portName));
    if(!modInst.getPort(portName).isOutPort()) {
      pin.getNet().removePin(pin);
      net.getLogicalNet().createPortInst(portName, modInst.getCellInst());
    }
    net.addPin(pin);
  }

  public static void main(String[] args){
    Design topDesign = new Design("topDesign", Device.PYNQ_Z1);
    EDIFNetlist netlist = topDesign.getNetlist();
    EDIFCell top = netlist.getTopCell();

    //Create New Design for target part: PYNQ_Z1
    Design or2_design = new Design("new_or2_design", topDesign.getPartName());
    Cell or2 = or2_design.createCell("or2", Unisim.OR2);
    boolean successPlaced = new DesignTools().placeCell(or2, or2_design);
    Cell ff = or2_design.createCell("ff", Unisim.FDRE);
    DesignTools.placeCell(ff, or2_design);

    //Connect "O" in or2 to "C" in ff
    Net or2_to_ff = or2_design.createNet("or2_to_ff");
    or2_to_ff.connect(or2, "O");
    or2_to_ff.connect(ff, "CE");

    Design d_1 = new BlockPlacer2().placeDesign(or2_design, false);
    d_1.setAutoIOBuffers(false);
    d_1.routeSites();
    new Router(d_1).routeDesign();
    d_1.writeCheckpoint("or2_design.dcp");

    //create an array of ports
    ArrayList<Port> modulePorts = new ArrayList<>();
    for(EDIFPort port : or2.getEDIFCellInst().getCellPorts()) {
      if (port.isOutput()) {
        continue;
      }
    	EDIFPort topPort = or2_design.getTopEDIFCell().createPort(port);
    	Net physNet = or2_design.createNet(port.getName());
    	physNet.getLogicalNet().createPortInst(topPort);
    	EDIFPortInst portInst = physNet.getLogicalNet().createPortInst(port, or2.getEDIFCellInst());

      String sitePinName = or2.getCorrespondingSitePinName(portInst.getName());
    	SitePinInst sitePin = new SitePinInst(portInst.isOutput(), sitePinName, or2.getSiteInst());
    	physNet.addPin(sitePin);
    	modulePorts.add(new Port(port.getName(), sitePin));
    }

    for(EDIFPort port : ff.getEDIFCellInst().getCellPorts()) {
      if (port.isInput()) {
        continue;
      }
    	EDIFPort topPort = or2_design.getTopEDIFCell().createPort(port);
    	Net physNet = or2_design.createNet(port.getName());
    	physNet.getLogicalNet().createPortInst(topPort);
    	EDIFPortInst portInst = physNet.getLogicalNet().createPortInst(port, ff.getEDIFCellInst());

      String sitePinName = ff.getCorrespondingSitePinName(portInst.getName());
    	SitePinInst sitePin = new SitePinInst(portInst.isOutput(), sitePinName, ff.getSiteInst());
    	physNet.addPin(sitePin);
    	modulePorts.add(new Port(port.getName(), sitePin));
    }

    Module or2_module = new Module(or2_design);
    or2_module.setPorts(modulePorts);
    //or2_module.setNetlist(or2_design.getNetlist());


		for(EDIFCell cell : or2_design.getNetlist().getWorkLibrary().getCells()){
			topDesign.getNetlist().getWorkLibrary().addCell(cell);
		}
		EDIFLibrary hdi = topDesign.getNetlist().getHDIPrimitivesLibrary();
		for(EDIFCell cell : or2_design.getNetlist().getHDIPrimitivesLibrary().getCells()){
			if(!hdi.containsCell(cell)) hdi.addCell(cell);
		}
    ModuleInst mi_1 = topDesign.createModuleInst("mi_1", or2_module);
    mi_1.place(or2_module.getAnchor().getSite());

    Cell b0 = topDesign.createAndPlaceIOB("button0", PinType.IN , "D19",  "LVCMOS33");
    //Cell b1 = topDesign.createAndPlaceIOB("button1", PinType.IN , "D18",  "LVCMOS33");
    Cell out0    = topDesign.createAndPlaceIOB("out0"   , PinType.OUT, "R14",  "LVCMOS33");

    // Connect Button 0 to the LUT2 input I0
    Net net0 = topDesign.createNet("mi_1_in");
    net0.connect(b0, "O");
    connectModuleInstPin(net0, "I0", mi_1);
    connectModuleInstPin(net0, "I1", mi_1);

    // Connect Button 0 to the LUT2 input I0
    Net net1 = topDesign.getNet("mi_1/Q");
    connectModuleInstPin(net1, "Q", mi_1);

    // We need some additional calls here to connect things properly because of the
    // added hierarchy of the LUT (OR2) module
    EDIFNet out = top.createNet("O");
    out.createPortInst("Q", mi_1.getCellInst());
    out.createPortInst("I",out0);
    net1.createPin(false, out0.getCorrespondingSitePinName("I"), out0.getSiteInst());
    //ModuleInst mi_2 = topDesign.createModuleInst("or2_mi_2", or2_module);
    //mi_2.place(or2_module.getAnchor().getSite());

    // Connect from "Q" in or_1 to  "I0" in or_2


    Design d = new BlockPlacer2().placeDesign(topDesign, false);
    d.setAutoIOBuffers(false);
    //d.routeSites();
    //new Router(d).routeDesign();
    d.writeCheckpoint("or_module_top_design.dcp");

  }
}
/*
Cell b0 = or2_design.createAndPlaceIOB("button0", PinType.IN , "D19",  "LVCMOS33");
Cell b1 = or2_design.createAndPlaceIOB("button1", PinType.IN , "D18",  "LVCMOS33");
Cell out0    = or2_design.createAndPlaceIOB("out0"   , PinType.OUT, "R14",  "LVCMOS33");

// Connect Button 0 to the LUT2 input I0
Net net0 = or2_design.createNet("button0_IBUF");
net0.connect(b0, "O");
net0.connect(or2, "I0");

// Connect Button 1 to the LUT2 input I1
Net net1 = or2_design.createNet("button1_IBUF");
net1.connect(b1, "O");
net1.connect(or2, "I1");

// Connect the LUT2 (AND2) to the LED IO
Net net2 = or2_design.createNet("or2");
net2.connect(or2, "O");
net2.connect(out0, "I");
*/
