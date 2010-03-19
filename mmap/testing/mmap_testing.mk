$(eval $(call test,mmap_test,mmap arch,boost))
$(eval $(call test,pvo_test, mmap arch jmvcc,boost))

