# BypassLSU — lec inconclusive

kind=mod

| stage | result |
|---|---|
| yosys+slang gate | PASS |
| --reader slang -> prp | PASS |
| slang -> lg | PASS |
| prp -> lg | PASS |
| yosys-slang -> lg | PASS |
| lec prp vs slang | TIMEOUT |
| lec prp vs yosys-slang | REFUTED |
| abc gen | NA |

**First failure message:**
```
(none captured)
```
