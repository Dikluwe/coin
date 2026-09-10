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

#ifdef _MSC_VER
#pragma warning(disable:4786) // symbol truncated
#endif // _MSC_VER

#include <cassert>
#include <cstring>

#include <Inventor/C/XML/document.h>
#include <Inventor/C/XML/element.h>
#include <Inventor/C/XML/attribute.h>

#include <Inventor/scxml/ScXMLStateMachine.h>
#include <Inventor/scxml/ScXMLDocument.h>

#include "threads/threadsutilp.h"

#include "scxml/ScXMLP.h"

#define SCXML_DEFAULT_NS "default" // to be deprecated

// private static variables
ScXMLP::NamespaceDict * ScXMLP::namespaces = NULL;
ScXMLP::TargettypeDict * ScXMLP::targettypes = NULL;
ScXMLP::TypeDict * ScXMLP::profileevaluators = NULL;

// *************************************************************************

// *************************************************************************
// TYPE SYSTEM REGISTERING AND LOOKUP
// *************************************************************************

/*
*/
void
ScXMLP::registerClassType(const char * xmlns, const char * classname, SoType type)
{
  assert(!type.isBad());
  ScXMLP::registerClassType(ScXMLP::namespaces, xmlns, classname, type);
}

/*
*/
void
ScXMLP::registerInvokeClassType(const char * xmlns, const char * targettype, const char * source, SoType type)
{
  assert(!type.isBad());
  assert(xmlns);

  // Note that we actually split on targettype on the top of the
  // hierarchy/tree here, and not on xml-namespace as one might think
  // at first, but this is intentional. This is because the namespace
  // logic should fall through to the default namespace if you don't
  // find the type you are looking for in the specified namespace, and
  // because of that, it is most convenient to split on namespace at
  // the next-to-bottom level to be able to share code when looking up
  // types.

  ScXMLP::NamespaceDict * nsdict = ScXMLP::getNamespaceDict(ScXMLP::targettypes, targettype);
  assert(nsdict);

  ScXMLP::registerClassType(nsdict, xmlns, source, type);
}

/*
*/
void
ScXMLP::registerClassType(NamespaceDict * nsdict, const char * xmlns, const char * classname, SoType type)
{
  assert(xmlns && classname);

  TypeDict * typedict = ScXMLP::getTypeDict(nsdict, xmlns);
  assert(typedict);

  typedict->put(SbName(classname), type);
}

/*
*/
ScXMLP::TypeDict *
ScXMLP::getTypeDict(NamespaceDict * nsdict, const char * xmlns)
{
  assert(nsdict && xmlns);
  SbName key(xmlns);
  TypeDict * dict = NULL;
  if (nsdict->get(key, dict)) {
    return dict;
  }
  dict = new TypeDict;
  nsdict->put(key, dict);
  return dict;
}

// *************************************************************************
/*
*/
SoType
ScXMLP::getClassType(NamespaceDict * nsdict, const char * xmlns, const char * classname)
{
  assert(xmlns && classname);

  const SbName key(classname);
  SoType type = SoType::badType();

  ScXMLP::TypeDict * typedict = ScXMLP::getTypeDict(nsdict, xmlns);
  if (typedict && typedict->get(key, type)) {
    return type;
  }

  typedict = ScXMLP::getTypeDict(nsdict, SCXML_DEFAULT_NS);
  if (typedict && typedict->get(key, type)) {
    return type;
  }

  return SoType::badType();
}

/*
*/
ScXMLP::NamespaceDict *
ScXMLP::getNamespaceDict(TargettypeDict * ttdict, const char * targettype)
{
  assert(ttdict && targettype);
  SbName key(targettype);
  NamespaceDict * dict = NULL;
  if (ttdict->get(key, dict)) {
    return dict;
  }
  dict = new NamespaceDict;
  ttdict->put(key, dict);
  return dict;
}

/*
*/
SoType
ScXMLP::getClassType(const char * xmlns, const char * classname)
{
  return ScXMLP::getClassType(ScXMLP::namespaces, xmlns, classname);
}

SoType
ScXMLP::getInvokeClassType(const char * xmlns, const char * targettype, const char * source)
{
  NamespaceDict * nsdict = ScXMLP::getNamespaceDict(ScXMLP::targettypes, targettype);
  assert(nsdict);

  SoType invoketype = ScXMLP::getClassType(nsdict, xmlns, source);

#if 0
  if (invoketype == SoType::badType()) {
    return ScXMLFallbackInvoke::getClassTypeId();
  }
#endif

  return invoketype;
}

// *************************************************************************

// internal implementation
ScXMLStateMachine *
ScXMLP::readXMLData(cc_xml_doc * doc)
{
  const char * xmlns = SCXML_DEFAULT_NS;

  // get a handle on the root element
  cc_xml_elt * root = cc_xml_doc_get_root(doc);
  if (strcmp(cc_xml_elt_get_type(root), "scxml") != 0) {
    return NULL;
  }

  // peek at xmlns attribute to get the correct object types instantiated
  const cc_xml_attr * nsattr = cc_xml_elt_get_attribute(root, "xmlns");
  if (nsattr) {
    xmlns = cc_xml_attr_get_value(nsattr);
    assert(xmlns);
  }

  SoType statemachinetype = ScXMLP::getClassType(xmlns, "statemachine");
  assert(statemachinetype.canCreateInstance());
  ScXMLStateMachine * statemachine =
    static_cast<ScXMLStateMachine *>(statemachinetype.createInstance());
  assert(statemachine && statemachine->isOfType(ScXMLStateMachine::getClassTypeId()));

  // FIXME
  ScXMLObject * documentobj = NULL;
  // ScXMLObject * documentobj = ScXMLP::readScXMLDocument(statemachine, root, xmlns);
  assert(documentobj && documentobj->isOfType(ScXMLDocument::getClassTypeId()));
  ScXMLDocument * document = static_cast<ScXMLDocument *>(documentobj);

  statemachine->setDescription(document);

  return statemachine;
}

// *************************************************************************

void * ScXMLP::mutex = NULL;

void
ScXMLP::init(void)
{
  ScXMLP::namespaces = new ScXMLP::NamespaceDict;
  ScXMLP::targettypes = new ScXMLP::TargettypeDict;
  ScXMLP::profileevaluators = new ScXMLP::TypeDict;
  CC_MUTEX_CONSTRUCT(ScXMLP::mutex);
}

void
ScXMLP::cleanup(void)
{
  cleanup_namespacedict(ScXMLP::namespaces);
  delete ScXMLP::namespaces;
  ScXMLP::namespaces = NULL;
  delete ScXMLP::profileevaluators;
  ScXMLP::profileevaluators = NULL;
  cleanup_targettypes();
  CC_MUTEX_DESTRUCT(ScXMLP::mutex);
}

void
ScXMLP::lock(void)
{
  assert(ScXMLP::mutex);
  CC_MUTEX_LOCK(ScXMLP::mutex);
}

void
ScXMLP::unlock(void)
{
  assert(ScXMLP::mutex);
  CC_MUTEX_UNLOCK(ScXMLP::mutex);
}


void
ScXMLP::cleanup_namespacedict(NamespaceDict * dict)
{
  assert(dict);
  for (NamespaceDict::const_iterator it = dict->const_begin();
       it != dict->const_end();
       ++it) {
    delete it->obj;
  }
}

void
ScXMLP::cleanup_targettypes(void)
{
  assert(ScXMLP::targettypes);
  for (TargettypeDict::const_iterator it = ScXMLP::targettypes->const_begin();
       it != ScXMLP::targettypes->const_end();
       ++it) {
    ScXMLP::NamespaceDict * dict = it->obj;
    cleanup_namespacedict(dict);
    delete dict;
  }
  delete ScXMLP::targettypes;
  ScXMLP::targettypes = NULL;
}

// *************************************************************************
