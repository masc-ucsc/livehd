#!/usr/bin/python3

"""
Pandoc filter to process code blocks with class "graph" into
graphviz-generated images.
Requires pygraphviz, pandocfilters
"""

import os
import sys

import pygraphviz

from pandocfilters import toJSONFilter, Para, Image, get_filename4code
from pandocfilters import get_caption, get_extension, get_value

def document_name():
    if "PANDOC_INPUT_FILE" in os.environ:
        return os.environ["PANDOC_INPUT_FILE"]
    else:
        return "graph"

def graphviz(key, value, format, _):
    if key == 'CodeBlock':
        [[ident, classes, keyvals], code] = value
        if "graph" in classes:
            caption, typef, keyvals = get_caption(keyvals)
            prog, keyvals = get_value(keyvals, u"prog", u"dot")
            filetype = get_extension(format, "svg", html="svg", latex="pdf")
            dest = get_filename4code(document_name(), code, filetype)

            if not os.path.isfile(dest):
                g = pygraphviz.AGraph(string=code)
                g.layout()
                g.draw(dest, prog=prog)
                sys.stderr.write('Created image ' + dest + '\n')

            image = Image([ident, classes, keyvals],
                          caption,
                          [dest, typef])

            return Para([image])

if __name__ == "__main__":

    toJSONFilter(graphviz)
