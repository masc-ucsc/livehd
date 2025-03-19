import yaml
import argparse
import sys

# Create a custom representer for block scalars
def block_scalar_representer(dumper, value):
    # Remove any trailing newline characters to ensure YAML uses "|"
    value = value.rstrip('\n').rstrip()  # Also remove trailing spaces
    return dumper.represent_scalar('tag:yaml.org,2002:str', value, style='|')


def generate_yaml(input_file, top_name, critical_comment_present, model_name_version, output_file):
    try:

        with open(input_file, "r") as f:
            code_content = f.read()
        # Add the representer to handle multi-line strings as block scalars
        yaml.add_representer(str, block_scalar_representer)

        critical_comment_present=critical_comment_present.strip().lower()  # Convert to lowercase for consistency

        if critical_comment_present=="true":
            
            yaml_content = {
                    
                    "replicate_code_prompt1": [
                        {
                            "role": "system", 
                            "content": "You are a super smart Verilog and timing expert."
                            "You have been tasked with improving the frequency of a verilog code."
                            "You provide a higher frequency code which passes LEC."
                            "Make sure that you only return the code that passes LEC."
                            "Take care that:"
                            "The semantics are  preserved exactly as in the original netlist (including word instantiation and sign‐extension)"
                            "while breaking a long combinational critical path."
                            "The resultant code is functionally equivalent to the original and passes LEC."
                            },
                        {
                            "role": "user", 
                            "content": "This is the current Verilog:\n```\n{code_content}\n```\n"
                            "The above code has comments with the word CRITICAL providing hints on the where the critical path resides. These are likely statements or related statements that need to be optimized."
                            "Please do not change semantics, just split the always blocks in separate always blocks "
                            "and try to improve the performance when possible."
                            }
                        ]
                    }
        else:
            yaml_content = {
                    
                    "replicate_code_prompt1": [
                        {
                            "role": "system", 
                            "content": "You are a super smart Verilog and timing expert."
                            "You have been tasked with improving the frequency of a verilog code."
                            "You provide a higher frequency code which passes LEC."
                            "Make sure that you only return the code that passes LEC."
                            "Take care that:"
                            "The semantics are  preserved exactly as in the original netlist (including word instantiation and sign‐extension)"
                            "while breaking a long combinational critical path."
                            "The resultant code is functionally equivalent to the original and passes LEC."
                            },
                        {
                            "role": "user", 
                            "content": "This is the current Verilog:\n```\n{code_content}\n```\n"
                            "Please do not change semantics, just split the always blocks in separate always blocks "
                            "and try to improve the performance when possible."
                            }
                        ]
                    }
            
        yaml_content["top_name"]=top_name
        yaml_content["llm"]= {"model": model_name_version}
        if "o3" in model_name_version:
            #yaml_content["replicate_code"]["threshold"] = 40
            yaml_content["threshold"] = 40
        elif "o4" in model_name_version:
            yaml_content["temperature"] = 40
            #yaml_content["replicate_code"]["temperature"] = 40

        yaml_content["code_content"]=code_content
        #yaml_content["replicate_code"]["code_content"]=code_content
        
        with open(output_file, "w") as f:
            yaml.dump(yaml_content, f, default_flow_style=False, sort_keys=False, allow_unicode=True, default_style=None, indent=2)
        
        print(f"YAML file '{output_file}' generated successfully.")

    except FileNotFoundError as e:
        print(f"Error: The input file '{input_file}' was not found.")
        sys.exit(1)
    except IOError as e:
        print(f"Error: There was an issue with reading or writing files: {e}")
        sys.exit(1)
    except TypeError as e:
        print(f"Error: Type error encountered: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    try:
        parser = argparse.ArgumentParser(description="Generate a YAML file from an input file and model name version.")
        parser.add_argument("input_file", help="Path to the input file containing code.")
        parser.add_argument("top_name", help="top_name.")
        parser.add_argument("-c", "--critical_comment_present", required=True, help="whether the files are commented or not. true/false")
        parser.add_argument("-m", "--model_name_version", required=True, help="Model name as string.")
        parser.add_argument("-o", "--output_file", required=True, help="Name of the output YAML file (as module name preferred).")
    
        args = parser.parse_args()
        print("I GOT:")
        print(f"{args.input_file}, {args.top_name}, {args.critical_comment_present}, {args.model_name_version}, {args.output_file}\n")
        generate_yaml(args.input_file, args.top_name, args.critical_comment_present, args.model_name_version, args.output_file)
    except TypeError as e:
        print(f"Error: Invalid argument type passed to argparse: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred while parsing arguments: {e}")
        sys.exit(1)

