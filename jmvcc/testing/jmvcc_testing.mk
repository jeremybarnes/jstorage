CURRENT_TEST_TARGETS := jmvcc_test

$(eval $(call test,object_test,jmvcc arch boost_thread-mt,boost))
$(eval $(call test,versioned_test,jmvcc arch boost_thread-mt,boost))
$(eval $(call test,epoch_compression_test,jmvcc arch boost_thread-mt,boost))
$(eval $(call test,garbage_test,jmvcc arch boost_thread-mt,boost))
$(eval $(call test,sandbox_test,jmvcc arch boost_thread-mt,boost))
$(eval $(call test,version_table_test,jmvcc arch boost_thread-mt,boost))
