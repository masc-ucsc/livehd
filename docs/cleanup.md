
## Small Cleanup Tasks

This document has small cleanup/refactoring tasks


Classes should have only first character upper case. E.g:

* Rename LGraph to Lgraph in all the classes
* Rename Node_Internal to Node_internal
* Rename Index_ID to Index_id

Replace all the calls that return a pointer for XXX * ref_XXX

Replace all the get_text for get_sview (explicit to std::string_view)

Increase the token position to 64 bits

