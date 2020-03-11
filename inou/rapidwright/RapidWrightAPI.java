package com.xilinx.rapidwright.examples;
import org.graalvm.nativeimage.IsolateThread;
import org.graalvm.nativeimage.ObjectHandle;
import org.graalvm.nativeimage.ObjectHandles;
import org.graalvm.nativeimage.StackValue;
import org.graalvm.nativeimage.c.CContext;
import org.graalvm.nativeimage.c.constant.CConstant;
import org.graalvm.nativeimage.c.constant.CEnum;
import org.graalvm.nativeimage.c.constant.CEnumLookup;
import org.graalvm.nativeimage.c.constant.CEnumValue;
import org.graalvm.nativeimage.c.function.CEntryPoint;
import org.graalvm.nativeimage.c.function.CEntryPointLiteral;
import org.graalvm.nativeimage.c.function.CFunction;
import org.graalvm.nativeimage.c.function.CFunctionPointer;
import org.graalvm.nativeimage.c.function.InvokeCFunctionPointer;
import org.graalvm.nativeimage.c.struct.AllowWideningCast;
import org.graalvm.nativeimage.c.struct.CField;
import org.graalvm.nativeimage.c.struct.CFieldAddress;
import org.graalvm.nativeimage.c.struct.CFieldOffset;
import org.graalvm.nativeimage.c.struct.CStruct;
import org.graalvm.nativeimage.c.struct.SizeOf;
import org.graalvm.nativeimage.c.type.CCharPointer;
import org.graalvm.nativeimage.c.type.CIntPointer;
import org.graalvm.nativeimage.c.type.CLongPointer;
import org.graalvm.nativeimage.c.type.CTypeConversion;
import org.graalvm.nativeimage.c.type.CTypeConversion.CCharPointerHolder;
import org.graalvm.word.PointerBase;
import org.graalvm.word.SignedWord;
import org.graalvm.word.UnsignedWord;
import org.graalvm.word.WordFactory;

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


public class RapidWrightAPI {
  private static List<Design> DESIGN_ID_LIST = new ArrayList<Design>();
  private static List<Cell> CELL_ID_LIST = new ArrayList<Cell>();
  private static List<Module> MODULE_ID_LIST = new ArrayList<Module>();
  private static List<ModuleInst> MODULEINST_ID_LIST = new ArrayList<ModuleInst>();
    /*
     *java functions to create a new Design
     */
    @CEntryPoint( name = "RW_create_Design")
    public static int RW_create_Design(IsolateThread thread, CCharPointer design_name)
    {
      String desName = CTypeConversion.toJavaString(design_name);
      Design design = new Design(desName, Device.PYNQ_Z1);
      //inserts the specific ID in the arraylist
      DESIGN_ID_LIST.add(design);
      int id = DESIGN_ID_LIST.size() - 1;
      return id;
    }

    /*
     *java functions to create a flipflop on the design.
     */

    @CEntryPoint( name = "RW_create_FF")
    public static int RW_Create_FF(IsolateThread thread, CCharPointer flipflopName_c, int design_ID)
    {
      Design design = DESIGN_ID_LIST.get(design_ID);
      String ffName = CTypeConversion.toJavaString(flipflopName_c);
      Cell ff = design.createCell(ffName, Unisim.FDRE);
      CELL_ID_LIST.add(ff);
      return CELL_ID_LIST.size() - 1;

    }

    /*
     *java functions to create an AND gate on the design
     */
    @CEntryPoint( name = "RW_create_AND2")
    public static int RW_Create_AND2(IsolateThread thread, CCharPointer gateName_c, int design_ID)
    {

      Design design = DESIGN_ID_LIST.get(design_ID);
      String gateName = CTypeConversion.toJavaString(gateName_c);
      Cell and2 = design.createCell(gateName, Unisim.AND2);
      CELL_ID_LIST.add(and2);
      return CELL_ID_LIST.indexOf(and2);
    }

    /*
     *java functions to create module inherit from design and build on a new topdesign
     */
    @CEntryPoint( name = "RW_create_Module")
    public static int RW_create_Module(IsolateThread thread, int design_ID, int top_design_ID)
    {

      Design topDesign = DESIGN_ID_LIST.get(top_design_ID);
      Design design = DESIGN_ID_LIST.get(design_ID);
      Module module = new Module(design);

      module.setNetlist(design.getNetlist());
      for(EDIFCell cell : design.getNetlist().getWorkLibrary().getCells()){
			topDesign.getNetlist().getWorkLibrary().addCell(cell);
  		}
  		EDIFLibrary hdi = topDesign.getNetlist().getHDIPrimitivesLibrary();
  		for(EDIFCell cell : design.getNetlist().getHDIPrimitivesLibrary().getCells()){
  			if(!hdi.containsCell(cell)) hdi.addCell(cell);
  		}

      MODULE_ID_LIST.add(module);
      return MODULE_ID_LIST.size() - 1;
    }

    /*
     *java functions to create module instance from a existing module on a given topdesign
     */
    @CEntryPoint( name = "RW_create_ModuleInst")
    public static int RW_create_ModuleInst(IsolateThread thread, CCharPointer modInst_name, int module_ID, int top_design_ID)
    {

      Design topDesign = DESIGN_ID_LIST.get(top_design_ID);
      Module module = MODULE_ID_LIST.get(module_ID);
      System.out.println("module and design have been found");
      ModuleInst moduleInst = topDesign.createModuleInst("moduleInst", module);
      moduleInst.place(module.getAnchor().getSite());

      MODULEINST_ID_LIST.add(moduleInst);
      return MODULEINST_ID_LIST.size() - 1;
    }
    /*
     *A separte function to place cells
     */
    @CEntryPoint( name = "RW_place_Cell")
    public static void RW_place_Cell(IsolateThread thread, int cell_ID, int design_ID)
    {

      Design design = DESIGN_ID_LIST.get(design_ID);
      Cell cell = CELL_ID_LIST.get(cell_ID);
      DesignTools.placeCell(cell, design);

    }

    @CEntryPoint( name = "RW_set_IO_Buffer")
    public static boolean RW_set_IO_Buffer(IsolateThread thread, boolean bool, int design_ID)
    {
      Design design = DESIGN_ID_LIST.get(design_ID);
      design.setAutoIOBuffers(bool);
      return bool;
    }

    @CEntryPoint( name = "RW_place_Design")
    public static void RW_place_Design(IsolateThread thread, int design_ID)
    {
      Design design = DESIGN_ID_LIST.get(design_ID);
      Design placed_design = new BlockPlacer2().placeDesign(design, false);
    }

    @CEntryPoint( name = "RW_costum_Route")
    public static void RW_costumRoute(IsolateThread thread, int design_ID, int src_ID, int snk_ID)
    {
      Design design = DESIGN_ID_LIST.get(design_ID);
      Cell src = CELL_ID_LIST.get(src_ID);
      Cell snk = CELL_ID_LIST.get(snk_ID);
      Net customRoutedNet = design.createNet("src");
      customRoutedNet.connect(src, "Q");
		  customRoutedNet.connect(snk, "D");
    }

    @CEntryPoint( name = "RW_route_Design")
    public static void RW_route_Design(IsolateThread thread, int design_ID)
    {
      Design design = DESIGN_ID_LIST.get(design_ID);
      design.routeSites();
  		new Router(design).routeDesign();
    }


    @CEntryPoint( name = "RW_write_DCP")
    public static void RW_write_DCP(IsolateThread thread, CCharPointer file_name, int design_ID)
    {
      Design design = DESIGN_ID_LIST.get(design_ID);
      String fName = CTypeConversion.toJavaString(file_name);
      design.writeCheckpoint(fName);
    }


    /*Create an example design with an AND Gate place on the cell and generate its dcp file*/
    @CEntryPoint( name = "addLogicGate")
    public static void addLogicGate(IsolateThread thread, CCharPointer deviceName, CCharPointer gateName)
    {
      String devName = CTypeConversion.toJavaString(deviceName);
      Design design = null;
      try
      {
        design = new Design(devName, Device.PYNQ_Z1);
      } catch(IllegalArgumentException e)
      {
        e.printStackTrace();
      }
      System.out.println(devName);
      String gName = CTypeConversion.toJavaString(gateName);
      Cell or2 = design.createAndPlaceCell(gName, Unisim.AND2, "SLICE_X100Y100/A6LUT");
      System.out.println(gName);
      design.setAutoIOBuffers(false);
      design.writeCheckpoint("and2.dcp");
    }

}
