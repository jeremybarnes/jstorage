$(eval $(call test,storage_test,storage arch,boost))
$(eval $(call test,snapshot_test,storage arch utils,boost))
$(eval $(call test,sigsegv_test,storage arch utils boost_thread-mt,boost))
