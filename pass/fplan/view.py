#!/usr/bin/env python3

# the PyCairo package needs to be installed as a dep, see https://pycairo.readthedocs.io/en/latest/getting_started.html for details

import cairo
import argparse

parser = argparse.ArgumentParser(description='Render .flp file to .png')
parser.add_argument('-f', '--file', type=str,
                    help='a .flp file to render')
parser.add_argument('-x', '--width', type=int,
                    help='width of png output image', default=1200)
parser.add_argument('-y', '--height', type=int,
                    help='height of png output image', default=1200)

args = parser.parse_args()

WIDTH = args.width
HEIGHT = args.height

if args.width < 50 or args.height < 50:
    print('image is too small!')
    exit(1)

surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, WIDTH, HEIGHT)
ctx = cairo.Context(surf)

with open(args.file, 'r') as flp:

    max_x = 0
    max_y = 0

    flp_lines = flp.readlines()
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
    
    print('maximum width is ' + str(max_x))
    print('maximum height is ' + str(max_y))

    # coords now go from 0 -> 1
    extra = 0.0
    ctx.scale(WIDTH / max_x, HEIGHT / max_y)

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

        print(tok_list[0] + ': start_x = ' + tok_list[3] + ', start_y = ' + tok_list[4] + ', width = ' + tok_list[1] + ', height = ' + tok_list[2])

        ctx.set_source_rgb(1, 1, 1)
        ctx.rectangle(start_x, start_y, start_x + width, start_y + height)
        ctx.fill_preserve() # fill rectangle, preserving border

        ctx.set_source_rgb(0, 0, 0)
        ctx.set_line_width(0.00005)
        ctx.stroke() # generate border

        ctx.set_source_rgb(1, 0, 1)
        ctx.set_font_size(0.0005)
        ctx.select_font_face('Courier', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)

        extent = ctx.text_extents(name)

        if extent.width <= width and extent.height <= height:
            ctx.move_to(start_x + width / 2 - extent.width / 2, start_y + height / 2 - extent.height / 2)
            ctx.show_text(name)
        else:
            print('skipping name ' + name)

surf.write_to_png(args.file.split('.')[0] + '.png')
