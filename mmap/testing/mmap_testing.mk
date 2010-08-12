$(eval $(call test,mmap_test,mmap arch,boost))
$(eval $(call test,pvo_test, mmap arch jmvcc boost_thread-mt,boost))
$(eval $(call test,trie_test,mmap arch utils,boost))

