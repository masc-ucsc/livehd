import re
import os
import sys

"""
HOW TO RUN:
    python3 comment_rtl.py <name of log file generated from SynAlign>"
OUTPUT:
    each original file shall have a _commented.v file generated for it
"""

def extract_line_numbers_and_paths(log_file):
    """
    Parses the log file to extract line numbers and corresponding file paths.
    Returns a dictionary where keys are file paths and values are sets of line numbers.
    """
    with open(log_file, 'r') as f:
        lines = f.readlines()
    
    start_index = None
    for i, line in enumerate(lines):
        if "Reporting final critical resolved matches:" in line:
            start_index = i + 2  # Skip the header lines
            break
    
    if start_index is None:
        print("No relevant section found in the log file.")
        return {}
    
    file_line_map = {}
    for line in lines[start_index:]:
        # Extract line number and file path from the log line
        match = re.search(r'--\s+\[(\d+),\d+\](\S+)', line)
        if match:
            line_num, file_path = int(match.group(1)), match.group(2)
            if file_path not in file_line_map:
                file_line_map[file_path] = set()
            file_line_map[file_path].add(line_num)
    
    return file_line_map

def modify_rtl_v(file_line_map):
    """
    Modifies the specified files by appending "//CRITICAL" to the given line numbers.
    Saves the modified file as "_commented.v" in the same directory as the original file.
    """
    for rtl_file, line_numbers in file_line_map.items():
        try:
            with open(rtl_file, 'r') as f:
                lines = f.readlines()
            
            for line_num in line_numbers:
                if 1 <= line_num <= len(lines):  # Ensure the line number is within range
                    if "//CRITICAL" not in lines[line_num - 1]:
                        lines[line_num - 1] = lines[line_num - 1].rstrip() + " //CRITICAL\n"
            
            # Save the modified file as "_commented.v" in the same directory
            dir_path = os.path.dirname(rtl_file)
            new_file = os.path.join(dir_path, os.path.basename(rtl_file).replace(".v", "_commented.v"))

            # Delete the existing "_commented.v" file if it exists
            if os.path.exists(new_file):
                os.remove(new_file)

            with open(new_file, 'w') as f:
                f.writelines(lines)
            print(f"Saved modified file as {new_file} at lines: {sorted(line_numbers)}")
        except FileNotFoundError:
            print(f"File not found: {rtl_file}")
        except Exception as e:
            print(f"Error modifying {rtl_file}: {e}")

def main():
    """
    Main function to handle script execution. Takes log file as a command-line argument.
    """
    if len(sys.argv) != 2:
        print("Usage: script.py <log_file>")
        sys.exit(1)
    
    log_file = sys.argv[1]
    file_line_map = extract_line_numbers_and_paths(log_file)
    if file_line_map:
        modify_rtl_v(file_line_map)
    else:
        print("No modifications made to files.")

if __name__ == "__main__":
    main()

