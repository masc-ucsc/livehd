'''This parser was developed inspired by lib_parser.py by Jim Wang from GitHub
Usage: python timing_parser.py /path/to/file/filename.lib'''

import json
import lib_parser
import argparse
import sys


# Get file name from args
if len(sys.argv) == 1:
    print ("No liberty file provided. Exiting..")
    sys.exit(1)
fname = sys.argv[1]

# Parse file using lib_parser
parsed = lib_parser.lib_file_t(fname)
timing_dict = {}

# Iterating through each cell
for cell in parsed.library.dt_cell:
    c = parsed.library.dt_cell[cell]
    timing_dict[cell] = {}

    # Only output pin 'o' has timing values, except DFF's which have
    # outputs in pin 'q'
    output_pin = c.dt_pin['o'] if 'o' in c.dt_pin else c.dt_pin['q']
    for o in output_pin.dt_timing:

        # Get timing sense and related pin
        related_pin = o.split('..')[0]
        timing_sense = o.split('..')[1]

        # Store to dict
        timing_dict[cell][related_pin] = {}
        timing_dict[cell][related_pin]['related_pin'] = related_pin
        timing_dict[cell][related_pin]['timing_sense'] = timing_sense

        # Get timing tables
        timing_obj = output_pin.dt_timing[o]
        tables = timing_obj.dt_table

        # Iterating through tables
        for table in tables:
            # print 'Table Name: ',table
            table_instance = tables[table]
            timing_dict[cell][related_pin][table] = {}

            # Fetch index 1, index 2 and values and clean up strings
            index_1 = str(table_instance.dt_param['index_1']).split('=')[1]
            index_1 = [float(x) for x in index_1.split(',')]

            index_2 = str(table_instance.dt_param['index_2']).split('=')[1]
            index_2 = [float(x) for x in index_2.split(',')]

            values = str(table_instance.dt_param['values']).split('=')[1]
            values = values.replace('\\', '')
            values = values.replace('"', '')
            values = [float(x) for x in values.split(',')]

            # Store to dict
            timing_dict[cell][related_pin][table]['index_1'] = index_1
            timing_dict[cell][related_pin][table]['index_2'] = index_2
            timing_dict[cell][related_pin][table]['values'] = values
if '/' in fname:
    fname = fname.split('/')[-1]
json_fname = 'output' + fname.split('.')[0] + '.json'
json.dump(timing_dict, open(json_fname, 'w'), indent=1)
print('\nJson ' + json_fname + ' created!')
