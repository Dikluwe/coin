/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

/*!
  \class SbPList SbPList.h Inventor/lists/SbPList.h
  \brief The SbPList class is a container class for void pointers.

  \ingroup coin_base

*/


#include <Inventor/lists/SbPList.h>
#include "lists/SoCallbackListP.h"

/*!
  \fn SbPList::SbPList(const int sizehint)

  This constructor initializes the internal allocated size for the
  list to \a sizehint. Note that the list will still initially contain
  zero items.

*/

/*!
  \fn void SbPList::append(void * item)

  Append \a item to the end of the list.

  Automatically allocates more items internally if needed.
*/

/*!
  \fn void * SbPList::get(const int index) const

  Returns element at \a index. Does \e not expand array bounds if \a
  index is outside the list.
*/

/*!
  \fn void SbPList::set(const int index, void * item)

  Index operator to set element at \a index. Does \e not expand array
  bounds if \a index is outside the list.
*/

/*!
  \fn void SbPList::removeFast(const int index)

  Remove the item at \a index, moving the last item into its place and
  truncating the list.
*/

/*!
  \fn int SbPList::getLength(void) const

  Returns number of items in the list.
*/

/*!
 \fn void SbPList::truncate(const int length, const int fit)

 Shorten the list to contain \a length elements, removing items from
 \e index \a length and onwards.

 If \a fit is non-zero, will also shrink the internal size of the
 allocated array. Note that this is much less efficient than not
 re-fitting the array size.
*/

/*!
  \fn void ** SbPList::getArrayPtr(const int start = 0) const

  Returns pointer to a non-modifiable array of the lists elements.
  \a start specifies an index into the array.

  The caller is \e not responsible for freeing up the array, as it is
  just a pointer into the internal array used by the list.
*/

/*!
  \fn void *& SbPList::operator[](const int index) const

  Returns element at \a index.

  Will automatically expand the size of the internal array if \a index
  is outside the current bounds of the list. The values of any
  additional pointers are then set to \c NULL.
*/

/*!
  \fn SbBool SbPList::operator!=(const SbPList & l) const

  Inequality operator. Returns \c TRUE if this list and \a l are not
  equal.
*/

/*!
  \fn void SbPList::expand(const int size)

  Expand the list to contain \a size items. The new items added at the
  end have undefined value.
*/

/*!
  \fn int SbPList::getArraySize(void) const

  Return number of items there's allocated space for in the array.

  \sa getLength()
*/

/*!
  Default constructor.
*/
SbPList::SbPList(const int sizehint)
  : itembuffersize(DEFAULTSIZE), numitems(0), itembuffer(builtinbuffer)
{
  if (sizehint > DEFAULTSIZE) this->grow(sizehint);
}

/*!
  Copy constructor.
*/
SbPList::SbPList(const SbPList & l)
  : itembuffersize(DEFAULTSIZE), numitems(0), itembuffer(builtinbuffer)
{
  this->copy(l);
}

/*!
  Destructor.
*/
SbPList::~SbPList()
{
  SoCallbackListP::clearData(this);
  if (this->itembuffer != builtinbuffer) delete[] this->itembuffer;
}

/*!
  Make this list a copy of \a l.
*/
void
SbPList::copy(const SbPList & l)
{
  if (this == &l) return;
  const int n = l.numitems;
  this->expand(n);
  for (int i = 0; i < n; i++) this->itembuffer[i] = l.itembuffer[i];
  SoCallbackListP::copyData(&l, this);
}

/*!
  Assignment operator
*/
SbPList &
SbPList::operator=(const SbPList & l)
{
  this->copy(l);
  return *this;
}

/*!
  Fit the allocated array exactly around the length of the list,
  discarding memory spent on unused pre-allocated array cells.

  You should normally not need or want to call this method, and it is
  only available for the sake of having the option to optimize memory
  usage for the unlikely event that you should throw around huge
  SbList objects within your application.
*/
void
SbPList::fit(void)
{
  const int items = this->numitems;

  if (items < this->itembuffersize) {
    void ** newitembuffer = this->builtinbuffer;
    if (items > DEFAULTSIZE) newitembuffer = new void*[items];

    if (newitembuffer != this->itembuffer) {
      for (int i = 0; i < items; i++) newitembuffer[i] = this->itembuffer[i];
    }

    if (this->itembuffer != this->builtinbuffer) delete[] this->itembuffer;
    this->itembuffer = newitembuffer;
    this->itembuffersize = items > DEFAULTSIZE ? items : DEFAULTSIZE;
  }
}

/*!
  Return index of first occurrence of \a item in the list, or -1 if \a
  item is not present.
*/
int
SbPList::find(const void * item) const
{
  for (int i = 0; i < this->numitems; i++)
    if (this->itembuffer[i] == item) return i;
  return -1;
}

/*!
  Insert \a item at index \a insertbefore.

  \a insertbefore should not be larger than the current number of
  items in the list.
*/
void
SbPList::insert(void * item, const int insertbefore) {
#ifdef COIN_EXTRA_DEBUG
  assert(insertbefore >= 0 && insertbefore <= this->numitems);
#endif // COIN_EXTRA_DEBUG
  if (this->numitems == this->itembuffersize) this->grow();

  for (int i = this->numitems; i > insertbefore; i--)
    this->itembuffer[i] = this->itembuffer[i-1];
  this->itembuffer[insertbefore] = item;
  this->numitems++;
}

/*!
  Removes an \a item from the list. If there are several items with
  the same value, removes the \a item with the lowest index.
*/
void
SbPList::removeItem(void * item)
{
  int idx = this->find(item);
#ifdef COIN_EXTRA_DEBUG
  assert(idx != -1);
#endif // COIN_EXTRA_DEBUG
  if (idx >= 0) {
    this->remove(idx);
  }
}

/*!
  Remove the item at \a index, moving all subsequent items downwards
  one place in the list.
*/
void
SbPList::remove(const int index)
{
#ifdef COIN_EXTRA_DEBUG
  assert(index >= 0 && index < this->numitems);
#endif // COIN_EXTRA_DEBUG
  this->numitems--;
  for (int i = index; i < this->numitems; i++)
    this->itembuffer[i] = this->itembuffer[i + 1];
}

/*!
  Equality operator. Returns \c TRUE if this list and \a l are
  identical, containing the exact same ordered set of elements.
*/
int
SbPList::operator==(const SbPList & l) const
{
  if (this == &l) return TRUE;
  if (this->numitems != l.numitems) return FALSE;
  for (int i = 0; i < this->numitems; i++)
    if (this->itembuffer[i] != l.itembuffer[i]) return FALSE;
  return TRUE;
}

// Expand list to the given size, filling in with NULL pointers.
void
SbPList::expandlist(const int size) const
{
  const int oldsize = this->getLength();
  SbPList * thisp = (SbPList *)this;
  thisp->expand(size);
  for (int i = oldsize; i < size; i++) (*thisp)[i] = NULL;
}

// grow allocated array, not number of items
void
SbPList::grow(const int size)
{
  // Default behavior is to double array size.
  if (size == -1) this->itembuffersize <<= 1;
  else if (size <= this->itembuffersize) return;
  else { this->itembuffersize = size; }

  void ** newbuffer = new void*[this->itembuffersize];
  const int n = this->numitems;
  for (int i = 0; i < n; i++) newbuffer[i] = this->itembuffer[i];
  if (this->itembuffer != this->builtinbuffer) delete[] this->itembuffer;
  this->itembuffer = newbuffer;
}

#ifdef COIN_TEST_SUITE

// Directed study coverage for SbPList's historical Open Inventor contracts.
// These tests are intentionally local to the component while we decide which
// behaviors are suitable for an upstream regression suite.

class SbPListTestAccess : public SbPList {
public:
  SbPListTestAccess(const int sizehint = 4) : SbPList(sizehint) { }

  int capacity(void) const { return this->getArraySize(); }
};

BOOST_AUTO_TEST_CASE(default_storage_and_append_contract)
{
  int a = 1;
  int b = 2;
  int c = 3;
  int d = 4;
  int e = 5;

  SbPListTestAccess list;
  BOOST_CHECK_EQUAL(list.getLength(), 0);
  BOOST_CHECK_EQUAL(list.capacity(), 4);

  list.append(&a);
  list.append(&b);
  list.append(&c);
  list.append(&d);
  BOOST_CHECK_EQUAL(list.capacity(), 4);

  list.append(&e);
  BOOST_CHECK_EQUAL(list.getLength(), 5);
  BOOST_CHECK_EQUAL(list.capacity(), 8);
  BOOST_CHECK(list.find(&a) == 0);
  BOOST_CHECK(list.find(&e) == 4);
  BOOST_CHECK(list.find(NULL) == -1);

  void ** array = list.getArrayPtr();
  BOOST_CHECK(array[0] == &a);
  BOOST_CHECK(array[4] == &e);
}

BOOST_AUTO_TEST_CASE(index_operator_expands_even_through_const_list)
{
  int value = 42;
  SbPList list;
  const SbPList & constlist = list;

  void *& slot = constlist[6];

  BOOST_CHECK_EQUAL(list.getLength(), 7);
  for (int i = 0; i < 7; i++) {
    BOOST_CHECK(list.get(i) == NULL);
  }
  BOOST_CHECK(slot == NULL);

  // This mutation through a const reference is part of the historical API.
  constlist[6] = &value;
  BOOST_CHECK(list.get(6) == &value);
  BOOST_CHECK_EQUAL(list.getLength(), 7);
}

BOOST_AUTO_TEST_CASE(insert_and_removal_contracts)
{
  int a = 1;
  int b = 2;
  int c = 3;
  int duplicate = 4;

  SbPList list;
  list.append(&a);
  list.append(&duplicate);
  list.append(&b);
  list.append(&duplicate);
  list.insert(&c, 2);

  BOOST_REQUIRE_EQUAL(list.getLength(), 5);
  BOOST_CHECK(list[0] == &a);
  BOOST_CHECK(list[1] == &duplicate);
  BOOST_CHECK(list[2] == &c);
  BOOST_CHECK(list[3] == &b);
  BOOST_CHECK(list[4] == &duplicate);

  list.removeItem(&duplicate);
  BOOST_REQUIRE_EQUAL(list.getLength(), 4);
  BOOST_CHECK(list[0] == &a);
  BOOST_CHECK(list[1] == &c);
  BOOST_CHECK(list[2] == &b);
  BOOST_CHECK(list[3] == &duplicate);

  list.remove(1);
  BOOST_REQUIRE_EQUAL(list.getLength(), 3);
  BOOST_CHECK(list[0] == &a);
  BOOST_CHECK(list[1] == &b);
  BOOST_CHECK(list[2] == &duplicate);

  list.removeFast(0);
  BOOST_REQUIRE_EQUAL(list.getLength(), 2);
  BOOST_CHECK(list[0] == &duplicate);
  BOOST_CHECK(list[1] == &b);
}

#if !defined(COIN_EXTRA_DEBUG)
BOOST_AUTO_TEST_CASE(removing_missing_item_preserves_list)
{
  int present = 1;
  int missing = 2;

  SbPList list;
  list.append(&present);
  list.removeItem(&missing);
  BOOST_REQUIRE_EQUAL(list.getLength(), 1);
  BOOST_CHECK(list[0] == &present);

  SbPList empty;
  empty.removeItem(&missing);
  BOOST_CHECK_EQUAL(empty.getLength(), 0);
}
#endif // !COIN_EXTRA_DEBUG

BOOST_AUTO_TEST_CASE(copy_assignment_and_self_assignment_are_independent)
{
  int a = 1;
  int b = 2;
  int replacement = 3;

  SbPList original;
  original.append(&a);
  original.append(&b);

  SbPList copy(original);
  SbPList assigned;
  assigned = original;
  assigned = assigned;

  original.set(0, &replacement);
  BOOST_CHECK(original[0] == &replacement);
  BOOST_CHECK(copy[0] == &a);
  BOOST_CHECK(assigned[0] == &a);
  BOOST_CHECK(copy == assigned);
  BOOST_CHECK(copy != original);
}

BOOST_AUTO_TEST_CASE(fit_moves_between_heap_and_builtin_storage)
{
  int values[9];
  SbPListTestAccess list;
  for (int i = 0; i < 9; i++) {
    values[i] = i;
    list.append(&values[i]);
  }
  BOOST_CHECK_EQUAL(list.capacity(), 16);

  list.truncate(6, TRUE);
  BOOST_CHECK_EQUAL(list.getLength(), 6);
  BOOST_CHECK_EQUAL(list.capacity(), 6);
  for (int i = 0; i < 6; i++) BOOST_CHECK(list[i] == &values[i]);

  list.truncate(3, TRUE);
  BOOST_CHECK_EQUAL(list.getLength(), 3);
  BOOST_CHECK_EQUAL(list.capacity(), 4);
  for (int i = 0; i < 3; i++) BOOST_CHECK(list[i] == &values[i]);
}

BOOST_AUTO_TEST_CASE(size_hint_reserves_without_changing_length)
{
  SbPListTestAccess small(1);
  BOOST_CHECK_EQUAL(small.getLength(), 0);
  BOOST_CHECK_EQUAL(small.capacity(), 4);

  SbPListTestAccess hinted(9);
  BOOST_CHECK_EQUAL(hinted.getLength(), 0);
  BOOST_CHECK_EQUAL(hinted.capacity(), 9);
}

#endif // COIN_TEST_SUITE
