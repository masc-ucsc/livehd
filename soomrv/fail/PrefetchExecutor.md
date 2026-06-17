# PrefetchExecutor — lg gen from slang fails

kind=mod

| stage | result |
|---|---|
| yosys+slang gate | PASS |
| --reader slang -> prp | PASS |
| slang -> lg | FAIL |
| prp -> lg | FAIL |
| yosys-slang -> lg | PASS |
| lec prp vs slang | NA |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
upass.tolg: combinational loop in 'PrefetchExecutor'
```
