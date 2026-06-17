# ACLINT — lec prp vs from slang fails

kind=mod

| stage | result |
|---|---|
| yosys+slang gate | PASS |
| --reader slang -> prp | PASS |
| slang -> lg | PASS |
| prp -> lg | PASS |
| yosys-slang -> lg | PASS |
| lec prp vs slang | REFUTED |
| lec prp vs yosys-slang | REFUTED |
| abc gen | NA |

**First failure message:**
```
(none captured)
```
