//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "inou_json.hpp"
#include "node.hpp"
#include "sub_node.hpp"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <iostream>

/*
UPDATE: Nov 10 - Summary of required output based on Yosys documentation

Yosys JSON general syntax (Described in Yosys documentation Appendix C.166 - write_json)

{
	"modules": {
		<module_name>: {
			"ports": {
				<port_name>: <port_details>,
				...
			},
			"cells": {
				<cell_name>: <cell_details>,
				...
			},
			"netnames": {
				<net_name>: <net_details>,
				...
			}
		}
	},
	"models": {
		...
	},
}

Where <port_details> is:

	{
		"direction": <"input" | "output" | "inout">,
		"bits": <bit_vector>
	}

And <cell_details> is :

	{
		"hide_name": <1 | 0>,
		"type": <cell_type>,
		"parameters": {
			<parameter_name>: <parameter_value>,
			...
		},
		"attributes": {
			<attribute_name>: <attribute_value>,
			...
		},
		"port_directions": {
			<port_name>: <"input" | "output" | "inout">,
			...
		},
		"connections": {
			<port_name>: <bit_vector>,
			...
		},
	}

And <net_details> is:

	{
		"hide_name": <1 | 0>,
		"bits": <bit_vector>
	}

The "hide_name" fields are set to 1 when the name of this cell or net is
automatically created and is likely not of interest for a regular user.

The "port_directions" section is only included for cells for which the
interface is known.

Module and cell ports and nets can be single bit wide or vectors of multiple
bits. Each individual signal bit is assigned a unique integer. The <bit_vector>
values referenced above are vectors of this integers. Signal bits that are
connected to a constant driver are denoted as string "0" or "1" instead of
a number.

Numeric parameter and attribute values up to 32 bits are written as decimal
values. Numbers larger than that are written as string holding the binary
representation of the value.
The models are given as And-Inverter-Graphs (AIGs) in the following form:

	"models": {
		<model_name>: [
			/* 0 * / [ <node-spec> ],
			/* 1 * / [ <node-spec> ],
			/* 2 * / [ <node-spec> ],
			...
		],
		...
	},
The following node-types may be used:

	[ "port", <portname>, <bitindex>, <out-list> ]
	 - the value of the specified input port bit

	[ "nport", <portname>, <bitindex>, <out-list> ]
	 - the inverted value of the specified input port bit

	[ "and", <node-index>, <node-index>, <out-list> ]
	 - the ANDed value of the specified nodes

	[ "nand", <node-index>, <node-index>, <out-list> ]
	 - the inverted ANDed value of the specified nodes

	[ "true", <out-list> ]
	 - the constant value 1

	[ "false", <out-list> ]
	 - the constant value 0

All nodes appear in topological order. I.e. only nodes with smaller indices
are referenced by "and" and "nand" nodes.

The optional <out-list> at the end of a node specification is a list of
output portname and bitindex pairs, specifying the outputs driven by this node.
Future version of Yosys might add support for additional fields in the JSON
format. A program processing this format must ignore all unknown fields.

*/

void to_json(LGraph *lg, const std::string &filename) {
	rapidjson::Document d;
	d.SetObject();
	rapidjson::Document::AllocatorType& dalloc = d.GetAllocator();
	rapidjson::Value modules;
	modules.SetObject();
   // UPDATE: Nov 17 - Found subnodes in the main lgraph. Might be able to extract suitable data	
	// Get root node
   Sub_node* top = lg->ref_self_sub_node();
   rapidjson::Value tmodule;
   tmodule.SetObject();
   std::string modname { top->get_name() }; // Root module name
   // UPDATE: Nov 22 - Figured out that I can get the pins required by Yosys. However, not the "bits" in each pin.
   // Yosys Ports == lgraph subnode pins
   rapidjson::Value ports;
   ports.SetObject();
   for(const auto &pin: top->get_io_pins()){
   	rapidjson::Value cpin;
   	cpin.SetObject();
   	rapidjson::GenericStringRef pdir(pin.dir == Sub_node::Direction::Input ? "input" : "output");
   	cpin.AddMember("direction", pdir, dalloc);
   	ports.AddMember(rapidjson::GenericStringRef(pin.name.c_str()), cpin, dalloc);
   }
   /*
   UPDATE: Nov 30 - Some issues ...
   
   Yosys cells =?? which equivalent in lgraph?
   Yosys netnames =?? which equivalent in lgraph?
   
   Looking at the code, it seems lgraph stored everything in a set of "edges" between "nodes".
   However, Yosys separates everything more semantically in the substructures defined as "ports", "cells", and "netnames".
   
   I need to find a suitable translation from one structure to the other ... I might not be able to finish on time
   */
   tmodule.AddMember("ports", ports, dalloc);
	modules.AddMember(rapidjson::GenericStringRef(modname.c_str()), tmodule, dalloc);
	d.AddMember("modules", modules, dalloc);
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	d.Accept(writer);
	std::cout << buffer.GetString() << std::endl;
}

