// See README.txt for information and build instructions.

#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <iostream>
#include <string>
#include <charconv>

#include "firrtl.pb.h"
#include "inou_firrtl.hpp"

using namespace std;

using google::protobuf::util::TimeUtil;

void Inou_firrtl::ListTypeInfo(const firrtl::FirrtlPB_Type& type) {
  switch (type.type_case()) {
    case 2: { //UInt type
      cout << "UInt[" << type.uint_type().width().value() << "]" << endl;
      break;

    } case 3: { //SInt type
      cout << "SInt[" << type.uint_type().width().value() << "]" << endl;
      break;

    } case 4: { //Clock type
      cout << "Clock" << endl;
      break;

    } case 5: { //Bundle type
      cout << "Bundle {" << endl;
      const firrtl::FirrtlPB_Type_BundleType btype = type.bundle_type();
      for (int i = 0; i < type.bundle_type().field_size(); i++) {
        cout << "\t" << btype.field(i).id() << ": ";
        ListTypeInfo(btype.field(i).type());
        /*for (int j = 0; j <= depth; j++) {
          cout << "\t";
        }
        cout << btype.field(i).id() << ": " << dir << "(" << btype.field(i).is_flipped()  << ")" << ", ";*/
        /*firrtl::Firrtl_Port_Direction field_dir = 0;
        if (btype.field(i).is_flipped()) {
          if (dir == 1) {
            field_dir = 2;
          } else if (dir == 2) {
            field_dir = 1;
          }0
        }*/
        //FIXME: Need to introduce is_flipped logic.... test on nested bundles
        //ListTypeInfo(btype.field(i).type(), btype.field(i).id(), dir, depth+1);
      }
      cout << "\t}\n";
      break;

    } case 6: { //Vector type
      const firrtl::FirrtlPB_Type_VectorType vtype = type.vector_type();
      cout << "Vector[" << vtype.size()  << "]" << endl;
      /*for (int j = 0; j <= depth+1; j++) {
        cout << "\t";
      }*/
      ListTypeInfo(vtype.type());
      break;

    } case 7: { //Fixed type
      cout << "Fixed[" << type.fixed_type().width().value() << "." << type.fixed_type().point().value() << "]" << endl;
      break;

    } case 8: { //Analog type
      cout << "Analog[" << type.uint_type().width().value() << "]" << endl;
      break;

    } case 9: { //AsyncReset type
      cout << "AsyncReset" << endl;
      break;

    } case 10: { //Reset type
      cout << "Reset" << endl;
      break;

    } default:
      cout << "Unknown port type." << endl;
      return;
  }
}

//void ListTypeInfo(const firrtl::Firrtl_Type& type, const string id, const firrtl::Firrtl_Port_Direction &dir, uint8_t depth) {
//  string dir_str;
//  if(dir == 1) {
//    dir_str = "input";
//  } else if (dir == 2) {
//    dir_str = "output";
//  } else {
//    dir_str = "unknown";
//  }
//
//  switch (type.type_case()) {
//    case 2: { //UInt type
//      cout << "UInt[" << type.uint_type().width().value() << "]" << endl;
//      break;
//
//    } case 3: { //SInt type
//      cout << "SInt[" << type.uint_type().width().value() << "]" << endl;
//      break;
//
//    } case 4: { //Clock type
//      cout << dir_str << " " << id << " : " << "Clock" << endl;
//      break;
//
//    } case 5: { //Bundle type
//      cout << "Bundle" << endl;
//      const firrtl::Firrtl_Type_BundleType btype = type.bundle_type();
//      for (int i = 0; i < type.bundle_type().field_size(); i++) {
//        for (int j = 0; j <= depth; j++) {
//          cout << "\t";
//        }
//        cout << btype.field(i).id() << ": " << dir << "(" << btype.field(i).is_flipped()  << ")" << ", ";
//        /*firrtl::Firrtl_Port_Direction field_dir = 0;
//        if (btype.field(i).is_flipped()) {
//          if (dir == 1) {
//            field_dir = 2;
//          } else if (dir == 2) {
//            field_dir = 1;
//          }0
//        }*/
//        //FIXME: Need to introduce is_flipped logic.... test on nested bundles
//        ListTypeInfo(btype.field(i).type(), btype.field(i).id(), dir, depth+1);
//      }
//      break;
//
//    } case 6: { //Vector type
//      const firrtl::Firrtl_Type_VectorType vtype = type.vector_type();
//      cout << "Vector[" << vtype.size()  << "]" << endl;
//      for (int j = 0; j <= depth+1; j++) {
//        cout << "\t";
//      }
//      ListTypeInfo(vtype.type(), "FIXME", dir, depth+1);
//      break;
//
//    } case 7: { //Fixed type
//      cout << "Fixed[" << type.fixed_type().width().value() << "." << type.fixed_type().point().value() << "]" << endl;
//      break;
//
//    } case 8: { //Analog type
//      cout << "Analog[" << type.uint_type().width().value() << "]" << endl;
//      break;
//
//    } case 9: { //AsyncReset type
//      cout << "AsyncReset" << endl;
//      break;
//
//    } case 10: { //Reset type
//      cout << "Reset" << endl;
//      break;
//
//    } default:
//      cout << "Unknown port type." << endl;
//      return;
//  }
//}

void Inou_firrtl::ListPortInfo(const firrtl::FirrtlPB_Port& port) {
  cout << "\t" << port.id() << ": " << port.direction() << ", ";
  ListTypeInfo(port.type());
}

void Inou_firrtl::IteratePrimOpExpr(const firrtl::FirrtlPB_Expression_PrimOp& op) {
  for (int i = 0; i < op.arg_size(); i++) {
    ListExprInfo(op.arg(i));
    cout << " ";
  }
}

void Inou_firrtl::ListPrimOpInfo(const firrtl::FirrtlPB_Expression_PrimOp& op) {
  //FIXME: Add stuff to this eventually;
  cout << "primOp: " << op.op();
  switch(op.op()) {
    case 1: { //Op_Add
      cout << "ADD\n";
      break;
    } case 2: { //Op_Sub
      cout << "SUB\n";
      break;
    } case 3: { //Op_Tail
      break;
    } case 4: { //Op_Head
      break;
    } case 5: { //Op_Times
      break;
    } case 6: { //Op_Divide
      break;
    } case 7: { //Op_Rem
      break;
    } case 8: { //Op_ShiftLeft
      break;
    } case 9: { //Op_Shift_Right
      break;
    } case 10: { //Op_Dynamic_Shift_Left
      break;
    } case 11: { //Op_Dynamic_Shift_Right
      break;
    } case 12: { //Op_Bit_And
      break;
    } case 13: { //Op_Bit_Or
      break;
    } case 14: { //Op_Bit_Xor
      break;
    } case 15: { //Op_Bit_Not
      break;
    } case 16: { //Op_Concat
      break;
    } case 17: { //Op_Less
      break;
    } case 18: { //Op_Less_Eq
      break;
    } case 19: { //Op_Greater
      break;
    } case 20: { //Op_Greater_Eq
      break;
    } case 21: { //Op_Equal
      cout << "EQUALS\n";
      break;
    } case 22: { //Op_Pad
      break;
    } case 23: { //Op_Not_Equal
      break;
    } case 24: { //Op_Neg
      break;
    } case 26: { //Op_Xor_Reduce
      break;
    } case 27: { //Op_Convert
      break;
    } case 28: { //Op_As_UInt
      break;
    } case 29: { //Op_As_SInt
      break;
    } case 30: { //Op_Extract_Bits
      break;
    } case 31: { //Op_As_Clock
      break;
    } case 32: { //Op_As_Fixed_Point
      break;
    } case 33: { //Op_And_Reduce
      break;
    } case 34: { //Op_Or_Reduce
      break;
    } case 35: { //Op_Increase_Precision
      break;
    } case 36: { //Op_Decrease_Precision
      break;
    } case 37: { //Op_Set_Precision
      break;
    } case 38: { //Op_As_Async_Reset
      break;
    } case 39: { //Op_Wrap
      break;
    } case 40: { //Op_Clip
      break;
    } case 41: { //Op_Squeeze
      break;
    } case 42: { //Op_As_interval
      break;
    } default:
      cout << "Unknown PrimaryOp\n";
      return;
  }
}

void Inou_firrtl::ListExprInfo(const firrtl::FirrtlPB_Expression& expr) {
  switch(expr.expression_case()) {
    case 1: { //Reference
      cout << expr.reference().id();
      break;

    } case 2: { //UIntLiteral
      cout << expr.uint_literal().value().value() << ".U("
           << expr.uint_literal().width().value() << ".W)";
      break;

    } case 3: { //SIntLiteral
      cout << expr.uint_literal().value().value() << ".S("
           << expr.uint_literal().width().value() << ".W)";
      break;

    } case 4: { //ValidIf
      //FIXME: What is this?
      break;

    } case 6: { //Mux
      cout << "Mux(t:";
      ListExprInfo(expr.mux().t_value());
      cout << ", f:";
      ListExprInfo(expr.mux().f_value());
      cout << ", c:";
      ListExprInfo(expr.mux().condition());
      cout << ");";
      break;

    } case 7: { //SubField
      ListExprInfo(expr.sub_field().expression());
      cout << "." << expr.sub_field().field();
      break;

    } case 8: { //SubIndex
      cout << "Subindex()\n";
      break;

    } case 9: { //SubAccess
      cout << "Subaccess()\n";
      break;

    } case 10: { //PrimOp
      ListPrimOpInfo(expr.prim_op());
      break;

    } case 11: { //FixedLiteral
      cout << expr.uint_literal().value().value() << " ("
           << expr.uint_literal().width().value() << "bits)";
      break;

    } default:
      cout << "Unknown expression type." << endl;
      return;
  }
}

void Inou_firrtl::ListStatementInfo(const firrtl::FirrtlPB_Statement& stmt) {
  //Print out statement
  switch(stmt.statement_case()) {
    case 1: { //Wire
      const firrtl::FirrtlPB_Statement_Wire& wire = stmt.wire();
      cout << wire.id() << " := Wire(";
      ListTypeInfo(wire.type());
      break;

    } case 2: { //Register
      cout << "Reg(";
      ListExprInfo(stmt.register_().clock());
      cout << ", ";
      ListExprInfo(stmt.register_().reset());
      cout << ", ";
      ListExprInfo(stmt.register_().init());
      cout << ");\n" << endl;
      break;

    } case 3: { //Memory
      break;

    } case 4: { //CMemory
      break;

    } case 5: { //Instance
      break;

    } case 6: { //Node
      cout << "Node(" << stmt.node().id();
      cout << ", ";
      ListExprInfo(stmt.node().expression());
      cout << ");\n";
      break;

    } case 7: { //When
      cout << "when(";
      ListExprInfo(stmt.when().predicate());
      cout << ") {\n";
      for (int i = 0; i < stmt.when().consequent_size(); i++) {
        ListStatementInfo(stmt.when().consequent(i));
      }
      if(stmt.when().otherwise_size() > 0) {
        cout << "} .otherwise {\n";
        for (int j = 0; j < stmt.when().otherwise_size(); j++) {
          ListStatementInfo(stmt.when().otherwise(j));
        }
      }
      cout << "}\n";

      break;

    } case 8: { //Stop
      break;

    } case 10: { //Printf
      break;

    } case 14: { //Skip
      cout << "skip;\n";
      break;

    } case 15: { //Connect
      ListExprInfo(stmt.connect().location());
      cout << " <= ";
      ListExprInfo(stmt.connect().expression());
      cout << ";\n";
      break;

    } case 16: { //PartialConnect
      ListExprInfo(stmt.connect().location());
      cout << " <- ";
      ListExprInfo(stmt.connect().expression());
      cout << ";\n";
      break;

    } case 17: { //IsInvalid
      break;

    } case 18: { //MemoryPort
      break;

    } case 20: { //Attach
      cout << "Attach\n";
      break;

    } default:
      cout << "Unknown statement type." << endl;
      return;
  }

  //Print out source info on the side
}

void Inou_firrtl::ListUserModuleInfo(const firrtl::FirrtlPB_Module& module) {
  cout << "Module (user): " << module.user_module().id() << endl;
  const firrtl::FirrtlPB_Module_UserModule& user_module = module.user_module();

  for (int i = 0; i < user_module.port_size(); i++) {
    const firrtl::FirrtlPB_Port& port = user_module.port(i);
    ListPortInfo(port);
  }

  for (int j = 0; j < user_module.statement_size(); j++) {
    const firrtl::FirrtlPB_Statement& stmt = user_module.statement(j);
    ListStatementInfo(stmt);
  }
}

void Inou_firrtl::ListModuleInfo(const firrtl::FirrtlPB_Module& module) {
  if(module.module_case() == 1) {
    cout << "External module.\n";
  } else if (module.module_case() == 2) {
    ListUserModuleInfo(module);
  } else {
    cout << "Module not set.\n";
  }
}

void Inou_firrtl::IterateModules(const firrtl::FirrtlPB_Circuit& circuit) {
  for (int i = 0; i < circuit.module_size(); i++) {
    ListModuleInfo(circuit.module(i));
  }
}

// Iterates though all people in the AddressBook and prints info about them.
void Inou_firrtl::IterateCircuits(const firrtl::FirrtlPB& firrtl_input) {
  for (int i = 0; i < firrtl_input.circuit_size(); i++) {
    const firrtl::FirrtlPB_Circuit& circuit = firrtl_input.circuit(i);
    IterateModules(circuit);






    //cout << "Module id: " << circuit.module(0).module_().id()  << endl;
    /*const tutorial::Person& person = address_book.people(i);

    cout << "Person ID: " << person.id() << endl;
    cout << "  Name: " << person.name() << endl;
    if (person.email() != "") {
      cout << "  E-mail address: " << person.email() << endl;
    }

    for (int j = 0; j < person.phones_size(); j++) {
      const tutorial::Person::PhoneNumber& phone_number = person.phones(j);

      switch (phone_number.type()) {
        case tutorial::Person::MOBILE:
          cout << "  Mobile phone #: ";
          break;
        case tutorial::Person::HOME:
          cout << "  Home phone #: ";
          break;
        case tutorial::Person::WORK:
          cout << "  Work phone #: ";
          break;
        default:
          cout << "  Unknown phone #: ";
          break;
      }
      cout << phone_number.number() << endl;
    }
    if (person.has_last_updated()) {
      cout << "  Updated: " << TimeUtil::ToString(person.last_updated()) << endl;
    }*/
  }
}

// Main function:  Reads the entire address book from a file and prints all
//   the information inside.
void setup_inou_firrtl() { Inou_firrtl::setup(); }

void Inou_firrtl::setup() {
  Eprp_method m1("inou.firrtl", "Translate FIRRTL to LNAST (in progress)", &Inou_firrtl::toLNAST);
  m1.add_label_required("files", "protobuf data files gotten from Chisel's toProto functionality");
  register_inou("firrtl", m1);
}

Inou_firrtl::Inou_firrtl(const Eprp_var &var) : Pass("firrtl", var) {
  if(var.has_label("files")) {
    auto file_name = var.get("files");
    std::string tmp;
    //std::from_chars(file_name.data(), file_name.data() + file_name.size(), tmp);
    for (const auto &f : absl::StrSplit(files, ",")) {
      cout << "FILE: " << f << "\n";
      firrtl::FirrtlPB firrtl_input;
      fstream input(std::string(f).c_str(), ios::in | ios::binary);
      if (!firrtl_input.ParseFromIstream(&input)) {
        cerr << "Failed to parse FIRRTL from protobuf format." << endl;
        return;
      }
      IterateCircuits(firrtl_input);
    }
  } else {
    cout << "No file provided. This requires a file input.\n";
    return;
  }

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();
}

void Inou_firrtl::toLNAST(Eprp_var &var) {
  Inou_firrtl p(var);
}

/*int main(int argc, char* argv[]) {
  // Verify that the version of the library that we linked against is
  // compatible with the version of the headers we compiled against.
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  if (argc != 2) {
    cerr << "Usage:  " << argv[0] << " FIRRTL_DATA_FILE" << endl;
    return -1;
  }

  //tutorial::AddressBook address_book;
  firrtl::Firrtl firrtl_input;

  {
    // Read the existing address book.
    fstream input(argv[1], ios::in | ios::binary);
    if (!firrtl_input.ParseFromIstream(&input)) {
      cerr << "Failed to parse FIRRTL from protobuf format." << endl;
      return -1;
    }
  }

  //ListPeople(address_book);
  IterateCircuits(firrtl_input);

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();

  return 0;
}*/
