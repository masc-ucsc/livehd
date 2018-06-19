
There are 3 directory categories for external source codes

## third_party/subs:

submodules that have no code change in relation with the provided repo This
should be for modules that are very stable

## third_party/misc:

External code libraries that have no external git copy. Either extensive
modifications or no longer maintained.

## external:

Forks from git repositories that have some minor patches. As such they can not
use subs and using misc is a waste to avoid external patches.

