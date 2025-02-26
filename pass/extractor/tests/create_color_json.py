import re
import json
import sys

def extract_nodes(timing_report_file):
    """
    Extract all unique node names from the timing report.
    Node names are expected to appear in lines with a leading '^' or 'v'
    and followed by a pattern like: node_name/CLK or similar.
    """
    nodes = []
    node_pattern = re.compile(r'[\^v]\s+(\w+)/')

    try:
        with open(timing_report_file, "r") as f:
            for line in f:
                matches = node_pattern.findall(line)
                for match in matches:
                    if match not in nodes:
                        nodes.append(match)
    except FileNotFoundError:
        print(f"Error: Timing report file '{timing_report_file}' not found.")
        sys.exit(1)

    return nodes

def create_color_json(module_name, nodes, output_file="color.json"):
    """
    Creates a color JSON file with all extracted nodes, each assigned a unique color value.
    Color values start at 1 and increment for each node.
    """
    node_colors = {node: i + 1 for i, node in enumerate(nodes)}

    data = [
        {
            "class": "livehd.lgraph.color",
            "modules": [
                {
                    "name": module_name,
                    "node_colors": node_colors
                }
            ]
        }
    ]

    with open(output_file, "w") as f:
        json.dump(data, f, indent=4)

    print(f"Created {output_file} with module '{module_name}' and {len(nodes)} nodes.")

def main():
    if len(sys.argv) != 3:
        print("Usage: {} <timing_report_file> <module_name>".format(sys.argv[0]))
        sys.exit(1)

    timing_report_file = sys.argv[1]
    module_name = sys.argv[2]

    nodes = extract_nodes(timing_report_file)
    create_color_json(module_name, nodes)

if __name__ == "__main__":
    main()

