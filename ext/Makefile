ARCH=$(shell uname -m)
TOPDIR = ..
SIMDEZIP = simde-r240327.zip
SIMDEDIR = $(SIMDEZIP:.zip=)
SUBDIR = bin include lib share

all: $(SUBDIR) include/simde/x86/avx2.h

$(SUBDIR):
	mkdir $@

include/simde/x86/avx2.h: tarball/$(SIMDEZIP)
	rm -rf $(SIMDEDIR)
	unzip -q tarball/$(SIMDEZIP)
	cp -r $(SIMDEDIR)/simde include

clean:  # this is a NOOP

format: # this is a NOOP

wipe:
	rm -rf bin include lib share $(CSVDIR) $(XRGDIR) $(JSONDIR) $(ARROWDIR)
