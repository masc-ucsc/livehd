import argparse
from graphviz import Digraph
import json

parser = argparse.ArgumentParser()
parser.add_argument('filename', help='input a json file')
args = parser.parse_args()

dot = Digraph(comment='lgraph')
with open(args.filename) as json_data:
    d = json.load(json_data)
if d["nodes"]:
    assert(isinstance(d["nodes"],list))
    for nodes in d["nodes"]:
        idx = str(nodes["idx"])
        inputs = nodes["inputs"]
        op = str(nodes["op"])
        outputs = nodes["outputs"]
        nodeName = idx + ": " + op
        if("input_name" in nodes):
            input_name = nodes["input_name"]
            nodeName += (": " + "input :" + input_name)
        if("output_name" in nodes):
            output_name = nodes["output_name"]
            nodeName += (": " + "output :" + output_name)
        print(nodeName)
        dot.node(idx,nodeName)
    for nodes in d["nodes"]:
        this_nid = str(nodes["idx"])
        for out_edges in nodes["outputs"]:
            out_nid = str(out_edges["out_nid"])
            src_port_id = str(out_edges["out_out_pid"])
            dst_port_id = str(out_edges["out_inp_pid"])
            edge_label = src_port_id + "->" + dst_port_id
            dot.edge(this_nid, out_nid,label=edge_label)

dot.render('test-output/lgraph.gv', view=True)
