#!/usr/bin/env python3

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

with open(args.file, 'r') as flp:
    flp_lines = flp.readlines()

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

# TODO: stroke gets clipped if module is on an edge
SCALE_FACTOR = SIZE / max_dim

# scale so that maximum width/height gets scaled to the bottom/right side of the picture
ctx.scale(SCALE_FACTOR, SCALE_FACTOR)

# render floorplan modules
for line in flp_lines:
    if '#' in line:
        continue

    tok_list = line.replace('\n', '').split('\t')
    if len(tok_list) != 5:
        continue

    name = tok_list[0]
    width = float(tok_list[1])
    height = float(tok_list[2])
    start_x = float(tok_list[3]) # left x
    start_y = float(tok_list[4]) # bottom y

    if args.verbose:
        print(tok_list[0] + ': start_x = ' + tok_list[3] + ', start_y = ' + tok_list[4] + ', width = ' + tok_list[1] + ', height = ' + tok_list[2])

    ctx.set_source_rgb(1, 1, 1)
    ctx.rectangle(start_x, start_y, width, height)
    ctx.fill_preserve() # fill rectangle, preserving border

    ctx.set_source_rgb(0, 0, 0)
    ctx.set_line_width(5 / SCALE_FACTOR)
    ctx.stroke() # generate border

ctx.set_source_rgb(0, 0, 0)
ctx.set_font_size(args.fontsize / SCALE_FACTOR)
ctx.select_font_face('Courier', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)

# render floorplan names
for line in flp_lines:
    if '#' in line:
        continue

    tok_list = line.replace('\n', '').split('\t')
    if len(tok_list) != 5:
        continue

    name = tok_list[0]
    width = float(tok_list[1])
    height = float(tok_list[2])
    start_x = float(tok_list[3])
    start_y = float(tok_list[4])

    extent = ctx.text_extents(name)

    if (extent.width <= width and extent.height <= height):
        ctx.move_to(start_x + width / 2 - extent.width / 2, start_y + height / 2 + extent.height / 2)
        ctx.show_text(name)
    else:
        if args.verbose:
            print('skipping name ' + name)

surf.write_to_png(args.file.split('.')[0] + '.png')
