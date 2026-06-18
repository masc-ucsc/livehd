# StoreQueueBackend — lec prp vs from slang fails

kind=mod

| stage | result |
|---|---|
| yosys+slang gate | FAIL |
| --reader slang -> prp | PASS |
| slang -> lg | PASS |
| prp -> lg | PASS |
| yosys-slang -> lg | NA |
| lec prp vs slang | REFUTED |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
(none captured)
```
