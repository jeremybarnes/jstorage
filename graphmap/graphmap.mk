# graphmap.mk
# Jeremy Barnes, 18 February 2010
# Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

GRAPHMAP_SOURCES := \

GRAPHMAP_LINK :=

$(eval $(call library,graphmap,$(GRAPHMAP_SOURCES),$(GRAPHMAP_LINK)))


$(eval $(call include_sub_make,graphmap_testing,testing))
