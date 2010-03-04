$(eval $(call test,mmap_test,mmap arch,boost))
$(eval $(call test,persistent_versioned_object_test, mmap arch jmvcc,boost))

