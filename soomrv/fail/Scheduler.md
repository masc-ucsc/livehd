# Scheduler — prp gen fails

kind=mod

| stage | result |
|---|---|
| yosys+slang gate | FAIL |
| --reader slang -> prp | PASS |
| slang -> lg | PASS |
| prp -> lg | FAIL |
| yosys-slang -> lg | NA |
| lec prp vs slang | NA |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
read of undefined variable 'modTable' (not visible here: declared later, out of scope, or never)
```
