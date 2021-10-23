import sys
import re

def main():

    # for website color mapping
    tfprof_map = {'inou.fir_tolnast': 'static',
                  'pass.lnast_ssa' :  'static',
                  'core.hierarchy' :  'static',
                  'pass.lnast_tolg' : 'static',
                  'pass.cprop' :      'static',
                  'pass.firbits' :    'static',
                  'pass.firmap' :     'static',
                  'pass.bitwidth' :   'static',
                  'inou.cgen.verilog':'static',
                  'idle' :            'subflow   ',
                  'waiting' :         'cudaflow  '
            }

    frname = sys.argv[1]
    thread_number = int(sys.argv[2])
    fw = open("tprof.json", 'w+')
    fr  = open(frname, 'r')
    fw.write("[\n")
    fw.write("  {\n")
    fw.write("    \"executor\": \"8-threads\",\n")
    fw.write("    \"data\": [\n")

    # first parsing to know the last line of tid=k happened
    tid_to_last_line = {}
    loc = 1
    for line in fr:
        words = line.split()
        tid = words[1]
        tid_to_last_line.update({tid:loc})
        loc += 1

    for key in tid_to_last_line:
        print(key, ":", tid_to_last_line[key])



    thread_str = {}
    for i in range(thread_number):
        key = 'tid=' + str(i)
        thread_str.update({key: ""})
        thread_str[key] += ("      {\n")
        thread_str[key] += ("        \"worker\" : \"%s\",\n" %(key))
        thread_str[key] += ("        \"data\" : [\n")


    words = []
    loc = 1
    # move the read pointer back to line zero
    fr.seek(0)
    for line in fr:
        words = line.split()
        pname = words[0]
        tid   = words[1]
        beg   = words[6][5:]
        end   = words[7][3:]

        thread_str[tid] += ("          {\n")
        thread_str[tid] += ("            \"span\": [%s, %s],\n" %(beg, end))
        thread_str[tid] += ("            \"name\": \"%s\",\n"   %(pname))
        if (pname == "idle"):
            thread_str[tid] += ("            \"type\": \"subflow\"\n")
        elif (pname == "waiting"):
            thread_str[tid] += ("            \"type\": \"cudaflow\"\n")
        else:
            thread_str[tid] += ("            \"type\": \"static\"\n")
        # thread_str[tid] += ("            \"type\": \"%s\"\n"    %(tfprof_map[pname]))
        if (loc == tid_to_last_line[tid]):
           thread_str[tid] += ("          }\n")
        else:
           thread_str[tid] += ("          },\n")

        loc +=1



    for i in range(thread_number):
        key = 'tid=' + str(i)
        # add one dummy element to avoid json tailing comma problem lazily
        # thread_str[key] += ("          {\n")
        # thread_str[key] += ("            \"span\": [0,0],\n")
        # thread_str[key] += ("            \"name\": \"idle\",\n")
        # thread_str[key] += ("            \"type\": \"static\"\n" )
        # thread_str[key] += ("          }\n")
        # thread_str[key] += ("          \b\n")
        thread_str[key] += ("        ]\n")
        if (i != thread_number -1):
            thread_str[key] += ("      },\n")
        else:
            thread_str[key] += ("      }\n")


    for i in range(thread_number):
        key = 'tid=' + str(i)
        fw.write(thread_str[key])


    # # add one dummy element to avoid json tailing comma problem lazily
    # fw.write("      {\n")
    # fw.write("      }\n")

    fw.write("    ]\n")
    fw.write("  }\n")
    fw.write("]\n")

    # for key in thread_str:
    #     print(thread_str[key])
if __name__ == "__main__":
    main()
