# CacheArbiter — --reader slang fails (own file is yosys-slang-parseable)

kind=-

| stage | result |
|---|---|
| yosys+slang gate | FAIL |
| --reader slang -> prp | FAIL |
| slang -> lg | NA |
| prp -> lg | NA |
| yosys-slang -> lg | NA |
| lec prp vs slang | NA |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
'CacheArbiter' is not a valid top-level module
```
