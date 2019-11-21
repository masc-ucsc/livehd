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
import com.xilinx.rapidwright.design.Net;
import com.xilinx.rapidwright.design.PinType;
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

//import com.xilinx.rapidwright.examples.RapidWrightAPI.cInterfaceHeaderFunctions;

//@CContext(cInterfaceHeaderFunctions.class)
public class RapidWrightAPI {
  //static final HashMap<Integer, Design> DESIGN_ID_LIST = new HashMap<Integer, Design>(); //vector<>
  private static List<Design> DESIGN_ID_LIST = new ArrayList<Design>();
  private static List<Cell> CELL_ID_LIST = new ArrayList<Cell>();
  /*static class cInterfaceHeaderFunctions implements CContext.Directives {
      @Override
      public List<String> getHeaderFiles() {
          /*
           * The header file with the C declarations that are imported. We use a helper class that
           * locates the file in our project structure.
           */
  /*        return Collections.singletonList("</mnt/c/Users/27688/Desktop/Lgraph_rapidwright/xilinx/GraalVMExample/myObjects.h>");
      }
  }*/
  /* Import a C structure, with accessor methods for every field. */
    /*@CStruct("my_design")
    interface myDesign extends PointerBase {

        @CField("java_object_handle")
        ObjectHandle getJavaObject();

        @CField("java_object_handle")
        void setJavaObject(ObjectHandle value);
    }*/

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
      //DesignTools.placeCell(and2, design);
      CELL_ID_LIST.add(and2);
      return CELL_ID_LIST.indexOf(and2);
    }

    /*
     *A separte function to place cells
     */
    @CEntryPoint( name = "RW_place_Cell")
    public static boolean RW_place_Cell(IsolateThread thread, int cell_ID, int design_ID)
    {

      Design design = DESIGN_ID_LIST.get(design_ID);
      Cell cell = CELL_ID_LIST.get(cell_ID);
      System.out.println("cellID: " + cell_ID);
      System.out.println("cell's name: " + cell.getName());
      boolean placed = DesignTools.placeCell(cell, design);
      System.out.println(cell.getSiteName());
      return placed;

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

    @CEntryPoint( name = "RW_connect_Ports")
    public static void RW_connect_Ports(IsolateThread thread, int design_ID, int src_ID, CCharPointer src_port, int snk_ID, CCharPointer snk_port)
    {
      Design design = DESIGN_ID_LIST.get(design_ID);
      Cell src = CELL_ID_LIST.get(src_ID);
      String src_portName = CTypeConversion.toJavaString(src_port);
      if (src == null) {
        System.out.println("src and2 is null!!!");
        return;
      }
      Cell snk = CELL_ID_LIST.get(snk_ID);
      String snk_portName = CTypeConversion.toJavaString(snk_port);
      Net net = design.createNet(src_portName + "to" + snk_portName);
      net.connect(src, src_portName);
      net.connect(snk, snk_portName);
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
