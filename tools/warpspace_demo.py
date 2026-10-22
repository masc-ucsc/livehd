"""Generate a reproducible standalone LEF/DEF for mesh-warp experiments."""
import argparse
import pathlib
import random


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=pathlib.Path)
    parser.add_argument('--cells', type=int, default=20000)
    parser.add_argument('--seed', type=int, default=7)
    args = parser.parse_args()
    if args.cells < 1:
        parser.error('--cells must be positive')
    args.directory.mkdir(parents=True, exist_ok=True)
    lef = '''VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 1 ; WIDTH 0.1 ; SPACING 0.1 ; END M1
SITE row CLASS CORE ; SYMMETRY Y ; SIZE 1 BY 1 ; END row
'''
    for name, size, kind in [('cell', 1, 'CORE'), ('ram', 80, 'BLOCK')]:
        lef += f'MACRO {name}\n CLASS {kind} ; ORIGIN 0 0 ; SIZE {size} BY {size} ; SYMMETRY X Y ;\n'
        if kind == 'CORE':
            lef += ' SITE row ;\n'
        for pin, direction, x in [('A', 'INPUT', .1), ('Y', 'OUTPUT', .7)]:
            lef += f' PIN {pin} DIRECTION {direction} ; USE SIGNAL ;\n  PORT LAYER M1 ; RECT {x} 0.1 {x+.1} 0.2 ; END\n END {pin}\n'
        lef += f'END {name}\n'
    lef += 'END LIBRARY\n'
    (args.directory / 'demo.lef').write_text(lef)
    rng = random.Random(args.seed)
    parts = ['- m0 ram + FIXED ( 50000 700000 ) N ;',
             '- m1 ram + PLACED ( 450000 450000 ) N ;',
             '- m2 ram + PLACED ( 800000 80000 ) E ;',
             '- m3 ram + PLACED ( 800000 800000 ) N ;']
    clusters = [(250, 300), (650, 350), (500, 700)]
    for i in range(args.cells):
        cx, cy = clusters[i % len(clusters)]
        x = round(max(0, min(999, rng.gauss(cx, 35))) * 1000)
        y = round(max(0, min(999, rng.gauss(cy, 35))) * 1000)
        parts.append(f'- c{i} cell + PLACED ( {x} {y} ) N ;')
    nets = [f'- n{i} ( c{i} Y ) ( c{i+1} A ) ( c{i+2} A ) ( c{i+3} A ) ;' for i in range(0, args.cells-3, 4)]
    (args.directory / 'demo.def').write_text('''VERSION 5.8 ;
DIVIDERCHAR "/" ; BUSBITCHARS "[]" ;
DESIGN warp_demo ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000000 1000000 ) ;
''' + ''.join(f'ROW row{j} row 0 {j*1000} N DO 1000 BY 1 STEP 1000 0 ;\n' for j in range(1000)) + f'COMPONENTS {len(parts)} ;\n' + '\n'.join(parts) + f'\nEND COMPONENTS\nNETS {len(nets)} ;\n' + '\n'.join(nets) + '\nEND NETS\nEND DESIGN\n')
    rho = 1.3 * args.cells / (1000*1000 - 4*80*80)
    print(f'Generated {args.cells} cells and 4 macros (one FIXED) in {args.directory}')
    print(f'1.3 times global free-area density: --rho-max {rho:.8f}')


if __name__ == '__main__':
    main()
