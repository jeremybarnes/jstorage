# attr.mk
# Jeremy Barnes, 18 February 2010
# Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

ATTR_SOURCES := \
	attribute.cc \
	attribute_traits.cc \
	attribute_basic_types.cc \
	string_map.cc

ATTR_LINK :=  boost_date_time-mt

$(eval $(call library,attr,$(ATTR_SOURCES),$(ATTR_LINK)))


$(eval $(call include_sub_make,attr_testing,testing))
