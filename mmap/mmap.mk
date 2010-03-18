# mmap.mk
# Jeremy Barnes, 18 February 2010
# Copyright (c) 2010 Recoset.  All rights reserved.

MMAP_SOURCES := \
	pvo_manager.cc \
	pvo_store.cc \
	pvo.cc \
	typed_pvo.cc

MMAP_LINK := jmvcc

$(eval $(call library,mmap,$(MMAP_SOURCES),$(MMAP_LINK)))


$(eval $(call include_sub_make,mmap_testing,testing))
