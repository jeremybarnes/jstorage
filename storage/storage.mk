# storage.mk
# Jeremy Barnes, 18 February 2010
# Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

STORAGE_SOURCES := \
	snapshot.cc \
	sigsegv.cc

STORAGE_LINK :=  boost_date_time-mt

$(eval $(call library,storage,$(STORAGE_SOURCES),$(STORAGE_LINK)))


$(eval $(call include_sub_make,storage_testing,testing))
