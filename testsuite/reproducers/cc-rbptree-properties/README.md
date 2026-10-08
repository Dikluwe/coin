# cc_rbptree property experiment

Run `bash testsuite/reproducers/cc-rbptree-properties/run.sh` from this
worktree. The script compiles in `/dev/shm` and runs with ASan/UBSan.

The deterministic experiment performs 64 seeds × 4,000 operations. After
every insertion, removal, or clean, it compares size and the full multiset
of `(pointer, data)` pairs with an independent model. It also checks the
red-black color, parent, black-height, node-count, and search-order
invariants. The source includes `rbptree.cpp` in the test translation unit
to inspect the private node type without defining a second copy of it.

This is a lab experiment, not a change to the public test suite. It does
not test out-of-memory behavior or mutation from a traversal callback;
those have separate regressions in the integration lab.
