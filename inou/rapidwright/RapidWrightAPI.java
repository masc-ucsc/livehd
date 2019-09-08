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
import com.xilinx.rapidwright.design.NetType;
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
import com.xilinx.rapidwright.placer.blockplacer.BlockPlacer;
import com.xilinx.rapidwright.placer.blockplacer.BlockPlacer2;
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

import com.xilinx.rapidwright.examples.RapidWrightAPI.cInterfaceHeaderFunctions;

@CContext(cInterfaceHeaderFunctions.class)
public class RapidWrightAPI {
  //static final HashMap<Integer, Design> DESIGNID_LIST = new HashMap<Integer, Design>(); //vector<>
  private static ArrayList<Design> DESIGNID_LIST = new ArrayList<Design>();
  static class cInterfaceHeaderFunctions implements CContext.Directives {
      @Override
      public List<String> getHeaderFiles() {
          /*
           * The header file with the C declarations that are imported. We use a helper class that
           * locates the file in our project structure.
           */
          return Collections.singletonList("</mnt/c/Users/27688/Desktop/Lgraph_rapidwright/xilinx/GraalVMExample/myObjects.h>");
      }
  }
  /* Import a C structure, with accessor methods for every field. */
    @CStruct("my_design")
    interface myDesign extends PointerBase {

        @CField("java_object_handle")
        ObjectHandle getJavaObject();

        @CField("java_object_handle")
        void setJavaObject(ObjectHandle value);
    }

    /*
     *java functions to create a new Design
     */
    @CEntryPoint( name = "RW_Create_Design")
    public static int RW_Create_Design(IsolateThread thread, CCharPointer designName)
    {
      String desName = CTypeConversion.toJavaString(designName);
      Design design = new Design(desName, Device.PYNQ_Z1);
      //inserts the specific ID in the arraylist
      DESIGNID_LIST.add(design);
      int id = DESIGNID_LIST.size()-1;
      return id;
    }

    /*
     *java functions to create a flipflop on the design.
     */

    @CEntryPoint( name = "RW_Create_FF")
    public static void RW_Create_FF(IsolateThread thread, CCharPointer flipflopName, int designID)
    {
      Design d = DESIGNID_LIST.get(designID);
      EDIFNetlist netlist = d.getNetlist();
      EDIFCell top = netlist.getTopCell();

      EDIFCell ff = netlist.getHDIPrimitive(Unisim.FDRE);
      String ffName = CTypeConversion.toJavaString(flipflopName);
      EDIFCellInst ffInst = top.createChildCellInst(ffName, ff);

    }

    /*
     *java functions to create an and gate on the design
     */

    @CEntryPoint( name = "RW_Create_AND2")
    public static void RW_Create_AND2(IsolateThread thread, CCharPointer gateName, int designID)
    {

      Design d = DESIGNID_LIST.get(designID);
      EDIFNetlist netlist = d.getNetlist();
      EDIFCell top = netlist.getTopCell();
      EDIFCell and2Wrapper = new EDIFCell(netlist.getWorkLibrary(), "and2Wrapper");
      EDIFCellInst and2WrapperInst = top.createChildCellInst("and2WrapperInst", and2Wrapper);

      EDIFCell and2 = netlist.getHDIPrimitive(Unisim.AND2);
      String gName = CTypeConversion.toJavaString(gateName);
      EDIFCellInst and2Inst = and2Wrapper.createChildCellInst(gName,and2);

    }

    @CEntryPoint( name = "RW_set_IO_Buffer")
    public static boolean RW_set_IO_Buffer(IsolateThread thread, boolean bool, int designID)
    {
      Design d = DESIGNID_LIST.get(designID);
      d.setAutoIOBuffers(bool);
      return bool;
    }

    @CEntryPoint( name = "RW_place_Design")
    public static void RW_place_Design(IsolateThread thread, int designID)
    {
      Design d = DESIGNID_LIST.get(designID);
      Design placed_design = new BlockPlacer2().placeDesign(d, false);
    }

    @CEntryPoint( name = "RW_route_Design")
    public static void RW_route_Design(IsolateThread thread, int designID)
    {
      Design d = DESIGNID_LIST.get(designID);
      d.routeSites();
  		new Router(d).routeDesign();
    }


    @CEntryPoint( name = "RW_write_DCP")
    public static void RW_write_DCP(IsolateThread thread, CCharPointer fileName, int designID)
    {
      Design d = DESIGNID_LIST.get(designID);
      String fName = CTypeConversion.toJavaString(fileName);
      d.writeCheckpoint(fName);
    }


/*Create an example design with an AND Gate place on the cell and generate its dcp file*/
  @CEntryPoint( name = "addLogicGate")
  public static void addLogicGate(IsolateThread thread, CCharPointer deviceName, CCharPointer gateName)
  {
    String devName = CTypeConversion.toJavaString(deviceName);
    Design d = null;
    try
    {
      d = new Design(devName, Device.PYNQ_Z1);
    } catch(IllegalArgumentException e)
    {
      e.printStackTrace();
    }
    System.out.println(devName);
    String gName = CTypeConversion.toJavaString(gateName);
    Cell or2 = d.createAndPlaceCell(gName, Unisim.AND2, "SLICE_X100Y100/A6LUT");
    System.out.println(gName);
    d.setAutoIOBuffers(false);
    d.writeCheckpoint("and2.dcp");
  }

}
