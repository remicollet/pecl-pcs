
# Files generated by Windows build

WIN_CLEANUP = config.nice.bat Release_TS configure.bat configure.js

PHPC_EMBED = pcs://internal/tools/embed.php
PHPC_CMD = $(PHP_EXECUTABLE) -d "extension=pcs.$(SHLIB_DL_SUFFIX_NAME)" -r "require '$(PHPC_EMBED)';" --

.PHONY: phpc code

#-----------------------

phpc: code

code:
	$(PHPC_CMD) $(srcdir)/php/src $@ $(srcdir)/php/phpc/$@.phpc

#-----------------------

cleanup: clean
	phpize --clean
	@rm -rf $(WIN_CLEANUP) include modules
