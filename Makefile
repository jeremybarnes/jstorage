-include local.mk

default: all
.PHONY: default

BUILD   ?= build
ARCH    ?= $(shell uname -m)
OBJ     := $(BUILD)/$(ARCH)/obj
BIN     := $(BUILD)/$(ARCH)/bin
TESTS   := $(BUILD)/$(ARCH)/tests
SRC     := .

JML_TOP := jml

include $(JML_TOP)/arch/$(ARCH).mk

CXXFLAGS += -I. -Wno-deprecated -Wno-uninitialized -Winit-self -fno-omit-frame-pointer
CXXLINKFLAGS += -Ljml/../build/$(ARCH)/bin -Wl,--rpath,jml/../build/$(ARCH)/bin -Wl,--copy-dt-needed-entries

ifeq ($(MAKECMDGOALS),failed)
include .target.mk
failed:
        +make $(FAILED) $(GOALS)
else

include $(JML_TOP)/functions.mk
include $(JML_TOP)/rules.mk

$(shell echo GOALS := $(MAKECMDGOALS) > .target.mk)
endif

$(eval $(call include_sub_makes,attr))
$(eval $(call include_sub_makes,storage))
$(eval $(call include_sub_makes,jmvcc))
$(eval $(call include_sub_makes,mmap))
$(eval $(call include_sub_makes,graphmap))
