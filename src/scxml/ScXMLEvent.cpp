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

#include <Inventor/scxml/ScXMLEvent.h>

/*!
  \class ScXMLEvent ScXMLEvent.h Inventor/scxml/ScXMLEvent.h
  \brief Base class for events sent to SCXML state machines.

  \since Coin 3.0
  \ingroup coin_scxml
*/

#include <cassert>
#include <cstring>
#include <memory>
#include "misc/SbHashName.h"

#include <Inventor/scxml/ScXML.h>

// *************************************************************************

class ScXMLEvent::PImpl {
public:
  PImpl(void) { }
  ~PImpl(void)
  {
    for (SbHash<SbName, const char *>::const_iterator it = this->associations.const_begin();
         it != this->associations.const_end();
         ++it) {
      delete [] const_cast<char *>(it->obj);
    }
    this->associations.clear();
  }

  SbHash<SbName, const char *> associations;
};

#define PRIVATE(obj) ((obj)->pimpl)

SCXML_OBJECT_SOURCE(ScXMLEvent);

void
ScXMLEvent::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLEvent, ScXMLObject, "ScXMLObject");
}

void
ScXMLEvent::cleanClass(void)
{
  ScXMLEvent::classTypeId = SoType::badType();
}

ScXMLEvent::ScXMLEvent(void)
: name(SbName::empty())
{
}

ScXMLEvent::~ScXMLEvent(void)
{
}

/*!
  This method is for setting a string that will identify this particular
  event, having this particular state, and can be used for event matching
  in the SCXML descriptions.

  The string should, according to the specification, be a set of tokens consisting
  of alphanumeric characters, separated with periods (.). This limitation
  is not enforced by this implementation.
*/
void
ScXMLEvent::setEventName(const SbName & namearg)
{
  this->name = namearg;
}

void
ScXMLEvent::setAssociation(const char * key, const char * value)
{
  assert(key && value);
  SbName dictentry(key);
  // value may point into the association being replaced. Copy it first.
  std::unique_ptr<char[]> valuecopy(new char [strlen(value) + 1]);
  strcpy(valuecopy.get(), value);
  const char * existing = NULL;
  PRIVATE(this)->associations.get(dictentry, existing);
  PRIVATE(this)->associations.put(dictentry, valuecopy.get());
  valuecopy.release();
  delete [] existing;
}

const char *
ScXMLEvent::getAssociation(const char * key) const
{
  assert(key);
  SbName dictentry(key);
  const char * val = NULL;
  if (PRIVATE(this)->associations.get(dictentry, val)) {
    return val;
  }
  return NULL;
}

size_t
ScXMLEvent::getNumAssociations(void) const
{
  return PRIVATE(this)->associations.getNumElements();
}

size_t
ScXMLEvent::getAssociationKeys(SbList<const char *> & keys) const
{
  size_t count = 0;
  for (SbHash<SbName, const char *>::const_iterator it =
         PRIVATE(this)->associations.const_begin();
       it != PRIVATE(this)->associations.const_end();
       ++it) {
    keys.append(it->key.getString());
    ++count;
  }
  return count;
}

ScXMLEvent *
ScXMLEvent::clone(void) const
{
  SoType thistype = this->getTypeId();
  assert(thistype.canCreateInstance());
  ScXMLEvent * newevent = static_cast<ScXMLEvent *>(thistype.createInstance());
  newevent->copyContents(this);
  return newevent;
}

void
ScXMLEvent::copyContents(const ScXMLEvent * rhs)
{
  this->name = rhs->name;
  SbList<const char *> keys;
  size_t numkeys = rhs->getAssociationKeys(keys);
  for (size_t i = 0; i < numkeys; ++i) {
    const char * akey = keys[(int)i];
    this->setAssociation(akey, rhs->getAssociation(akey));
  }
}

#undef PRIVATE

#ifdef COIN_TEST_SUITE
#include <cstring>

BOOST_AUTO_TEST_CASE(association_replacement_aliases)
{
  ScXMLEvent event;
  event.setAssociation("key", "prefix-value");
  event.setAssociation("key", event.getAssociation("key"));
  BOOST_CHECK_MESSAGE(strcmp(event.getAssociation("key"), "prefix-value") == 0,
                      "replacing an association with its own value preserves it");
  event.setAssociation("key", event.getAssociation("key") + 7);
  BOOST_CHECK_MESSAGE(strcmp(event.getAssociation("key"), "value") == 0,
                      "a substring of the old value remains valid during replacement");
  BOOST_CHECK_MESSAGE(event.getNumAssociations() == 1, "replacement keeps one key");
  event.setAssociation("key", "");
  BOOST_CHECK_MESSAGE(strcmp(event.getAssociation("key"), "") == 0,
                      "empty association values are retained");
}

BOOST_AUTO_TEST_CASE(association_clone_owns_values)
{
  ScXMLEvent * original = new ScXMLEvent;
  original->setAssociation("first", "one");
  original->setAssociation("second", "two");
  ScXMLEvent * copy = original->clone();
  delete original;
  BOOST_CHECK_MESSAGE(strcmp(copy->getAssociation("first"), "one") == 0,
                      "clone owns its first association");
  BOOST_CHECK_MESSAGE(strcmp(copy->getAssociation("second"), "two") == 0,
                      "clone owns its second association");
  BOOST_CHECK_MESSAGE(copy->getNumAssociations() == 2, "clone retains both keys");
  delete copy;
}
#endif // COIN_TEST_SUITE
