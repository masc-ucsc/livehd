#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# the PyCairo package needs to be installed as a dep, see https://pycairo.readthedocs.io/en/latest/getting_started.html for details

# example usage from bash shell:
# > view.py -f 24 -s 1600 -i TRIPS.flp
# > open TRIPS.png

import cairo
import argparse

parser = argparse.ArgumentParser(description='Render .flp file to .png')
parser.add_argument('-i', '--file', type=str,
                    help='a .flp file to render')
parser.add_argument('-s', '--size', type=int,
                    help='height and width of png output image', default=1200)
parser.add_argument('-f', '--fontsize', type=int,
                    help='font size of all modules', default=25)
parser.add_argument('-d', '--detail', action='store_true',
                    help='render width and height of every element')
parser.add_argument('-v', '--verbose', action='store_true',
                    help='verbose output')

args = parser.parse_args()

SIZE = args.size

if args.size < 50:
    print('image is too small!')
    exit(1)

# (0, 0) is top left for png, maybe others?
surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, SIZE, SIZE)
ctx = cairo.Context(surf)

try:
    with open(args.file, 'r') as flp:
        flp_lines = flp.readlines()
except Exception:
    print("input file not found!")
    exit(1)

max_x = -1.0
max_y = -1.0

# find chip area
for line in flp_lines:
    if '#' in line or len(line) == 1:
        continue # line is a comment
    tok_list = line.replace('\n', '').split('\t')

    width = float(tok_list[1])
    height = float(tok_list[2])
    start_x = float(tok_list[3])
    start_y = float(tok_list[4])

    if width + start_x > max_x:
        max_x = width + start_x
    
    if height + start_y > max_y:
        max_y = height + start_y

if args.verbose:
    print('maximum width is ' + str(max_x))
    print('maximum height is ' + str(max_y))

# want uniform scaling in both dimensions, or things overlap and look weird
if max_x >= max_y:
    max_dim = max_x
else:
    max_dim = max_y

# stroke gets clipped if module is on an edge, but it's not very noticeable
SCALE_FACTOR = SIZE / max_dim

# scale so that maximum width/height gets scaled to the bottom/right side of the picture
ctx.scale(SCALE_FACTOR, SCALE_FACTOR)

ctx.set_font_size(args.fontsize / SCALE_FACTOR)
ctx.select_font_face('Courier', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)

namestack = []

# render floorplan modules
for line in flp_lines:
    if line[0] == '#':
        cmd = line[2:].split(" ", 1)[0] # check for specially formatted comments
        if cmd != 'start' and cmd != 'end':
            continue
        
        tok_list = line[2:].replace('\n', '').split(' ')
        action = tok_list[0]
        name = tok_list[1]

        if action == 'start':
            namestack.append(name)
        elif action == 'end':
            namestack.pop()
        
    else:
        tok_list = line.replace('\n', '').split('\t')
        if len(tok_list) != 5:
            continue

        name = tok_list[0]
        width = float(tok_list[1])
        height = float(tok_list[2])
        start_x = float(tok_list[3]) # left x
        start_y = float(tok_list[4]) # bottom y

        if args.verbose:
            print(tok_list[0] + ': (' + tok_list[3] + ', ' + tok_list[4] + ') + (' + tok_list[1] + ', ' + tok_list[2] + ') = (' + str(width + start_x) + ', ' + str(height + start_y) + ')')

        ctx.set_source_rgb(1, 1, 1)
        ctx.rectangle(start_x, start_y, width, height)
        ctx.fill_preserve() # fill rectangle, preserving border

        ctx.set_source_rgb(0, 0, 0)
        ctx.set_line_width(1 / SCALE_FACTOR)
        ctx.stroke() # generate border

        # render name
        fullname = name + "(" + namestack[-1] + ")"
        fextent = ctx.text_extents(fullname)
        nextent = ctx.text_extents(name)
        
        if fextent.width <= width and fextent.height <= height:
            ctx.move_to(start_x + width / 2 - fextent.width / 2, start_y + height / 2 + fextent.height / 2)
            ctx.show_text(fullname)
        elif nextent.width <= width and nextent.height <= height:
            ctx.move_to(start_x + width / 2 - nextent.width / 2, start_y + height / 2 + nextent.height / 2)
            ctx.show_text(name)
        else:
            if args.verbose:
                print('skipping name ' + name)

        if args.detail:
            wextent = ctx.text_extents(tok_list[1])
            hextent = ctx.text_extents(tok_list[2])
            
            # render width
            if wextent.width <= width and wextent.height <= height:
                ctx.move_to(start_x + width / 2 - wextent.width / 2, start_y + height / 10 + wextent.height / 2)
                ctx.show_text("{:.3f}".format(width * 1000))
            else:
                if args.verbose:
                    print('skipping width for ' + name)
        
            # render height
            if hextent.width <= width and hextent.height <= height:
                ctx.move_to(start_x + width / 10 - hextent.width / 2, start_y + height / 2 + hextent.height / 2)
                ctx.show_text("{:.3f}".format(height * 1000))
            else:
                if args.verbose:
                    print('skipping height for ' + name)

surf.write_to_png(args.file.split('.')[0] + '.png')
