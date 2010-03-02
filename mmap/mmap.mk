# mmap.mk
# Jeremy Barnes, 18 February 2010
# Copyright (c) 2010 Recoset.  All rights reserved.

MMAP_SOURCES := \

MMAP_LINK :=

$(eval $(call library,mmap,$(MMAP_SOURCES),$(MMAP_LINK)))


$(eval $(call include_sub_make,mmap_testing,testing))
