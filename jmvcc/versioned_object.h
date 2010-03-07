/* versioned_object.h                                              -*- C++ -*-
   Jeremy Barnes, 12 December 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Abstract interface to a versioned object.
*/

#ifndef __jmvcc__versioned_object_h__
#define __jmvcc__versioned_object_h__

#include <iostream>
#include <string>
#include "jmvcc_defs.h"


namespace JMVCC {


/*****************************************************************************/
/* VERSIONED_OBJECT                                                          */
/*****************************************************************************/

/// This is an actual object.  Contains metadata and value history of an
/// object.

struct Versioned_Object {

    virtual ~Versioned_Object()
    {
    }

    // Check that the given commit is possible to perform.  Should perform the
    // check as quickly as possible; it doesn't matter if it returns a false
    // positive (but false negatives are verbotim).  Used to abort a commit
    // early if there is no possibility of it working.
    // Should not modify anything.
    virtual bool check(Epoch old_epoch, Epoch new_epoch, void * data) const = 0;

    // Get the commit ready and check that everything can go ahead, but
    // don't actually perform the commit
    virtual bool setup(Epoch old_epoch, Epoch new_epoch, void * data) = 0;

    // Confirm a setup commit, making it permanent
    virtual void commit(Epoch new_epoch) throw () = 0;

    // Roll back a setup commit
    virtual void rollback(Epoch new_epoch, void * data) throw () = 0;

    // Clean up an unused version
    virtual void cleanup(Epoch unused_valid_from, Epoch trigger_epoch) = 0;
    
    // Rename an epoch to a different number.  Returns the valid_from value
    // of the next epoch in the set.
    virtual Epoch rename_epoch(Epoch old_valid_from, Epoch new_valid_from)
        throw () = 0;

    virtual void dump(std::ostream & stream = std::cerr, int indent = 0) const;

    virtual void dump_unlocked(std::ostream & stream = std::cerr,
                               int indent = 0) const;

    virtual std::string print_local_value(void * val) const;

    // Destroy the local object that was created.  Default does nothing;
    // normally it should simply run the constructor of the destroyed object.
    // Note that this should NOT free the value, just run any destructors
    // necessary.
    virtual void destroy_local_value(void * val) const;
};



} // namespace JMVCC


#endif /* __jmvcc__versioned_object_h__ */
