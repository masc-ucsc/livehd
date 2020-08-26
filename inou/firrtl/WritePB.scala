// See LICENSE for license details.

package firrtl
package transforms

// Compiler Infrastructure
import firrtl.{Transform, LowForm, HighForm, ChirrtlForm, CircuitState, Utils}
// Firrtl IR classes
import firrtl.ir.{DefModule, Statement, Expression, Mux}
// Map functions
import firrtl.Mappers._
import firrtl.stage.Forms
import firrtl.options.Dependency
// Scala's mutable collections
import scala.collection.mutable

import firrtl.proto.ToProto

class WriteLowPB extends Transform with DependencyAPIMigration {
  /** Requires the [[firrtl.ir.Circuit Circuit]] form to be "low" */
  override def prerequisites = Forms.LowForm
  override def optionalPrerequisites = Seq.empty
  override def optionalPrerequisiteOf = Forms.LowEmitters
  override def invalidates(a: Transform) = false

  def execute(state: CircuitState): CircuitState = {
    val circuit = state.circuit

    val ostream = new java.io.ByteArrayOutputStream()
    val protobuf = ToProto.writeToStream(ostream, circuit)

    val proto_file_name = circuit.main + ".lo.pb";
    val outputStream = new java.io.FileOutputStream(proto_file_name)
    ostream.writeTo(outputStream);

    state
  }
}

class WriteHighPB extends Transform with DependencyAPIMigration {
  /** Requires the [[firrtl.ir.Circuit Circuit]] form to be "high" */
  override def prerequisites = Forms.HighForm
  override def optionalPrerequisites = Seq.empty
  override def optionalPrerequisiteOf = Forms.HighEmitters
  override def invalidates(a: Transform) = false

  def execute(state: CircuitState): CircuitState = {
    val circuit = state.circuit

    val ostream = new java.io.ByteArrayOutputStream()
    val protobuf = ToProto.writeToStream(ostream, circuit)

    val proto_file_name = circuit.main + ".hi.pb";
    val outputStream = new java.io.FileOutputStream(proto_file_name)
    ostream.writeTo(outputStream);

    state
  }
}

class WriteChPB extends Transform with DependencyAPIMigration {
  /** Requires the [[firrtl.ir.Circuit Circuit]] form to be "chirrtl" */
  override def prerequisites = Forms.ChirrtlForm
  override def optionalPrerequisites = Seq.empty
  override def optionalPrerequisiteOf = Forms.ChirrtlEmitters
  override def invalidates(a: Transform) = false

  def execute(state: CircuitState): CircuitState = {
    val circuit = state.circuit

    val ostream = new java.io.ByteArrayOutputStream()
    val protobuf = ToProto.writeToStream(ostream, circuit)

    val proto_file_name = circuit.main + ".ch.pb";
    val outputStream = new java.io.FileOutputStream(proto_file_name)
    ostream.writeTo(outputStream);

    state
  }
}
