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

#include <Inventor/scxml/ScXMLCoinEvaluator.h>

/*!
  \class ScXMLCoinEvaluator ScXMLCoinEvaluator.h Inventor/scxml/ScXMLCoinEvaluator.h
  \brief implements the evaluator for the custom profile named "x-coin".

  The x-coin profile extends over the minimum profile with data interpretation,
  logical operators, arithmetic expressions, and some convenience data model
  features like access to the scene graph and temporary storage of variables.

  \ingroup coin_soscxml
*/

#include <cassert>
#include <cstring>
#include "misc/SbHashName.h"

#include <memory>

#include <Inventor/SoDB.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbString.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/fields/SoFieldContainer.h>
#include <Inventor/scxml/ScXML.h>
#include <Inventor/scxml/ScXMLDocument.h>
#include <Inventor/scxml/ScXMLEvent.h>
#include <Inventor/scxml/ScXMLAbstractStateElt.h>
#include <Inventor/scxml/ScXMLDataElt.h>
#include <Inventor/scxml/SoScXMLStateMachine.h>
#include "base/coinString.h"
#include "scxml/SbStringConvert.h"
#include "scxml/ScXMLCommonP.h"
#include "soscxml/eval-coin.h"

// *************************************************************************

class ScXMLCoinEvaluator::PImpl {
public:
  enum { SMALL_CAPACITY = 7 };

  struct Temporary {
    Temporary(void) : object(NULL) { }
    Temporary(const SbName & keyarg, ScXMLDataObj * objectarg)
      : key(keyarg), object(objectarg) { }

    SbName key;
    ScXMLDataObj * object;
  };

  typedef SbHash<SbName, ScXMLDataObj *> TemporaryMap;

  PImpl(void) : numsmalltemporaries(0), temporaries(NULL), usinghash(FALSE) { }
  ~PImpl(void);

  ScXMLDataObj * get(const SbName & key) const;
  void put(const SbName & key, ScXMLDataObj * object);
  void clear(void);
  void dump(void) const;

private:
  static void dumpTemporary(const SbName & key, ScXMLDataObj * object);

  Temporary smalltemporaries[SMALL_CAPACITY];
  int numsmalltemporaries;
  TemporaryMap * temporaries;
  SbBool usinghash;
};

ScXMLCoinEvaluator::PImpl::~PImpl(void)
{
  this->clear();
  delete this->temporaries;
}

ScXMLDataObj *
ScXMLCoinEvaluator::PImpl::get(const SbName & key) const
{
  if (this->usinghash) {
    ScXMLDataObj * object = NULL;
    return this->temporaries->get(key, object) ? object : NULL;
  }

  const int length = this->numsmalltemporaries;
  for (int i = 0; i < length; ++i) {
    if (this->smalltemporaries[i].key == key) {
      return this->smalltemporaries[i].object;
    }
  }
  return NULL;
}

void
ScXMLCoinEvaluator::PImpl::put(const SbName & key, ScXMLDataObj * object)
{
  if (this->usinghash) {
    ScXMLDataObj * existing = NULL;
    if (this->temporaries->get(key, existing)) delete existing;
    this->temporaries->put(key, object);
    return;
  }

  const int length = this->numsmalltemporaries;
  for (int i = 0; i < length; ++i) {
    if (this->smalltemporaries[i].key == key) {
      delete this->smalltemporaries[i].object;
      this->smalltemporaries[i].object = object;
      return;
    }
  }

  if (length < SMALL_CAPACITY) {
    this->smalltemporaries[this->numsmalltemporaries++] = Temporary(key, object);
    return;
  }

  if (this->temporaries == NULL) {
    this->temporaries = new TemporaryMap(11);
  }
  assert(this->temporaries->getNumElements() == 0);
  for (int i = 0; i < length; ++i) {
    const Temporary & temporary = this->smalltemporaries[i];
    this->temporaries->put(temporary.key, temporary.object);
  }
  this->numsmalltemporaries = 0;
  this->usinghash = TRUE;
  this->temporaries->put(key, object);
}

void
ScXMLCoinEvaluator::PImpl::clear(void)
{
  if (this->usinghash) {
    for (TemporaryMap::const_iterator it = this->temporaries->const_begin();
         it != this->temporaries->const_end(); ++it) {
      delete it->obj;
    }
    this->temporaries->clear();
    this->usinghash = FALSE;
    return;
  }

  const int length = this->numsmalltemporaries;
  for (int i = 0; i < length; ++i) {
    delete this->smalltemporaries[i].object;
  }
  this->numsmalltemporaries = 0;
}

void
ScXMLCoinEvaluator::PImpl::dumpTemporary(const SbName & key,
                                         ScXMLDataObj * object)
{
  if (object->isOfType(ScXMLConstantDataObj::getClassTypeId())) {
    SbString value;
    ScXMLConstantDataObj * cobj = static_cast<ScXMLConstantDataObj *>(object);
    cobj->convertToString(value);
    printf("> TEMP variable '%s' with value '%s'.\n",
           key.getString(), value.getString());
  } else {
    printf("> TEMP variable '%s' with unknown value.\n", key.getString());
  }
}

void
ScXMLCoinEvaluator::PImpl::dump(void) const
{
  if (this->usinghash) {
    for (TemporaryMap::const_iterator it = this->temporaries->const_begin();
         it != this->temporaries->const_end(); ++it) {
      dumpTemporary(it->key, it->obj);
    }
    return;
  }

  for (int i = 0; i < this->numsmalltemporaries; ++i) {
    const Temporary & temporary = this->smalltemporaries[i];
    dumpTemporary(temporary.key, temporary.object);
  }
}

#define PRIVATE(obj) ((obj)->pimpl)

SCXML_OBJECT_SOURCE(ScXMLCoinEvaluator);

void
ScXMLCoinEvaluator::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLCoinEvaluator, ScXMLEvaluator, "ScXMLEvaluator");
  // use this evaluator with the 'x-coin' profile
  ScXML::registerEvaluatorType(SbName("x-coin"), classTypeId);

  ScXMLCoinEqualsOpExprDataObj::initClass();
  ScXMLCoinAddOpExprDataObj::initClass();
  ScXMLCoinSubtractOpExprDataObj::initClass();
  ScXMLCoinMultiplyOpExprDataObj::initClass();
  ScXMLCoinDivideOpExprDataObj::initClass();
  ScXMLCoinLengthFuncExprDataObj::initClass();
}

void
ScXMLCoinEvaluator::cleanClass(void)
{
  ScXML::unregisterEvaluatorType(SbName("x-coin"), classTypeId);

  ScXMLCoinLengthFuncExprDataObj::cleanClass();
  ScXMLCoinDivideOpExprDataObj::cleanClass();
  ScXMLCoinMultiplyOpExprDataObj::cleanClass();
  ScXMLCoinSubtractOpExprDataObj::cleanClass();
  ScXMLCoinAddOpExprDataObj::cleanClass();
  ScXMLCoinEqualsOpExprDataObj::cleanClass();

  ScXMLCoinEvaluator::classTypeId = SoType::badType();
}

ScXMLCoinEvaluator::ScXMLCoinEvaluator(void)
{
}

ScXMLCoinEvaluator::~ScXMLCoinEvaluator(void)
{
}

void
ScXMLCoinEvaluator::setStateMachine(ScXMLStateMachine * sm)
{
  inherited::setStateMachine(sm);
  if (sm) {
    SbList<const char *> modules;
    modules.append("core");
    modules.append("data");
    modules.append("externals");
    sm->setEnabledModulesList(modules);
  }
}

/*!
*/
ScXMLDataObj *
ScXMLCoinEvaluator::evaluate(const char * expression) const
{
  return scxml_coin_parse(expression);
}

SbBool
ScXMLCoinEvaluator::setAtLocation(const char * location, ScXMLDataObj * obj)
{
  ScXMLStateMachine * sm = this->getStateMachine();
  // 1) CHECK 1 - location is temporary variable?
  if (strncmp(location, "coin:temp.", 10) == 0) {
    SbName varname(location + 10);

    ScXMLConstantDataObj * cobj = NULL;
    if (obj->getTypeId().isDerivedFrom(ScXMLConstantDataObj::getClassTypeId())) {
      cobj = static_cast<ScXMLConstantDataObj *>(obj);
    } else {
      if (!obj->getTypeId().isDerivedFrom(ScXMLExprDataObj::getClassTypeId())) {
        // illegal obj
        return FALSE;
      }
      ScXMLExprDataObj * expr = static_cast<ScXMLExprDataObj *>(obj);
      ScXMLDataObj * res = expr->evaluate(sm);
      if (!res) {
        // unable to evaluate
        return FALSE;
      }
      assert(res);
      if (!res->getTypeId().isDerivedFrom(ScXMLConstantDataObj::getClassTypeId())) {
        // did not evaluate
        return FALSE;
      }
      cobj = static_cast<ScXMLConstantDataObj *>(res);
    }
    assert(cobj);
    // Clone before replacing: cobj may refer to the value already stored here.
    ScXMLDataObj * copy = cobj->clone();
    PRIVATE(this)->put(varname, copy);
    return TRUE;
  }

  // 2) CHECK 2 - location is datamodel?
  if (strncmp(location, "_data.", 6) == 0) {
    SbName varname(location + 6);
    const ScXMLDocument * doc = sm->getDescription();
    ScXMLDataElt * dataelt = doc->getDataById(varname);
    if (!dataelt) {
      // no such datamodel variable
      return FALSE;
    }
    ScXMLConstantDataObj * cobj = NULL;
    if (obj->getTypeId().isDerivedFrom(ScXMLConstantDataObj::getClassTypeId())) {
      cobj = static_cast<ScXMLConstantDataObj *>(obj);
    } else {
      if (!obj->getTypeId().isDerivedFrom(ScXMLExprDataObj::getClassTypeId())) {
        // illegal obj
        return FALSE;
      }
      ScXMLExprDataObj * expr = static_cast<ScXMLExprDataObj *>(obj);
      ScXMLDataObj * res = expr->evaluate(sm);
      if (!res) {
        // unable to evaluate
        return FALSE;
      }
      assert(res);
      if (!res->getTypeId().isDerivedFrom(ScXMLConstantDataObj::getClassTypeId())) {
        // did not evaluate
        return FALSE;
      }
      cobj = static_cast<ScXMLConstantDataObj *>(res);
    }
    assert(cobj);
    SbString strval;
    cobj->convertToString(strval);
    dataelt->setExprAttribute(strval.getString());
    return TRUE;
  }

  if (strncmp(location, "_event.", 7) == 0) {
    // illegal assignment target
    return FALSE;
  }

  // fieldcontainers
  //   location is camera?
  //   location is scene graph?
  //   location is globalfield?

  SoFieldContainer * fieldcontainer = NULL;
  SbName fieldname(SbName::empty());

  if (strncmp(location, "coin:camera.", 12) == 0) {
    if (!sm->isOfType(SoScXMLStateMachine::getClassTypeId())) {
      // can't get camera from statemachine
      return FALSE;
    }
    SoScXMLStateMachine * statemachine = static_cast<SoScXMLStateMachine *>(sm);
    fieldcontainer = statemachine->getActiveCamera();
    fieldcontainer->ref();
    fieldname = SbName(location + 12);
  }
  else if (strncmp(location, "coin:scene.", 11) == 0) {
    if (!sm->isOfType(SoScXMLStateMachine::getClassTypeId())) {
      // can't get scene from statemachine
      return FALSE;
    }
    SoScXMLStateMachine * statemachine = static_cast<SoScXMLStateMachine *>(sm);
    fieldcontainer = statemachine->getSceneGraphRoot();
    fieldcontainer->ref();
    fieldname = SbName(location + 10);
  }
  else if (strncmp(location, "coin:global.", 12) == 0) {
    fieldname = SbName(location + 12);
    SoField * field = SoDB::getGlobalField(fieldname);
    if (!field) {
      // no such global field
      return FALSE;
    }

    fieldcontainer = field->getContainer();
    fieldcontainer->ref();
  }

  if (fieldcontainer) {
    fieldcontainer->unrefNoDelete();
  }

  return FALSE;
}

ScXMLDataObj *
ScXMLCoinEvaluator::locate(const char * location) const
{
  ScXMLStateMachine * sm = this->getStateMachine();
  assert(sm);

  const char * var = sm->getVariable(location);
  if (var) {
    // statemachine knows about this location.
    return this->evaluate(var);
  }

  if (strncmp(location, "coin:temp.", 10) == 0) {
    SbName varname(location + 10);
    return PRIVATE(this)->get(varname);
  }
  else if (strpbrk(location, ":.") == NULL) {
    // maybe implicit location path
    SbName varname(location);
    ScXMLDataObj * obj = PRIVATE(this)->get(varname);
    if (obj) return obj;
    // no return - passing through
  }

  if (strncmp(location, "_data.", 6) == 0) {
    SbName varname(location + 6);
    const ScXMLDocument * doc = sm->getDescription();
    ScXMLDataElt * dataelt = doc->getDataById(varname);
    if (!dataelt) {
      // no such datamodel variable
      //printf("error: could not find data named '%s'\n", varname.getString());
      return NULL;
    }
    const char * expr = dataelt->getExprAttribute();
    ScXMLDataObj * obj = this->evaluate(expr);
    if (!obj) {
      // could not evaluate
      //printf("error: could not evaluate data named '%s'\n", varname.getString());
      return NULL;
    }

    ScXMLConstantDataObj * cobj = NULL;
    if (obj->getTypeId().isDerivedFrom(ScXMLConstantDataObj::getClassTypeId())) {
      cobj = static_cast<ScXMLConstantDataObj *>(obj);
    } else {
      if (!obj->getTypeId().isDerivedFrom(ScXMLExprDataObj::getClassTypeId())) {
        // illegal obj
        //printf("error: neither constant not expression ('%s')\n", varname.getString());
        return NULL;
      }
      ScXMLExprDataObj * expr = static_cast<ScXMLExprDataObj *>(obj);
      ScXMLDataObj * res = expr->evaluate(sm);
      if (!res) {
        // unable to evaluate
        //printf("error: evaluate for ('%s') failed\n", varname.getString());
        return NULL;
      }
      assert(res);
      if (!res->getTypeId().isDerivedFrom(ScXMLConstantDataObj::getClassTypeId())) {
        // did not evaluate
        //printf("error: evaluate for ('%s') and still not constant\n", varname.getString());
        return NULL;
      }
      cobj = static_cast<ScXMLConstantDataObj *>(res);
    }
    assert(cobj);
    //printf("location for data %s, returning %s\n", varname.getString(), cobj->getTypeId().getName().getString());
    return cobj;
  }

  if (strncmp(location, "_event.", 7) == 0) {
    const ScXMLEvent * event = sm->getCurrentEvent();
    assert(event);
    //if (sm->isOfType(SoScXMLStateMachine::getClassTypeId())) {
    //  SoScXMLStateMachine * sosm = static_cast<SoScXMLStateMachine *>(sm);
    //}
    const char * assocname = location + 7;
    const char * assoc = event->getAssociation(assocname);
    if (!assoc) {
      // invalid event hook?
      return NULL;
    }
    ScXMLDataObj * dataobj = this->evaluate(assoc);
    if (!dataobj) {
      return NULL;
    }
    if (dataobj->getTypeId().isDerivedFrom(ScXMLExprDataObj::getClassTypeId())) {
      ScXMLExprDataObj * expr = static_cast<ScXMLExprDataObj *>(dataobj);
      dataobj = expr->evaluate(sm);
      if (!dataobj) {
        return NULL;
      }
    }
    if (!dataobj->getTypeId().isDerivedFrom(ScXMLConstantDataObj::getClassTypeId())) {
      return NULL;
    }
    return dataobj;
  }

  if (strncmp(location, "coin:camera.", 12) == 0) {
    // FIXME: implement proper action
  }
  if (strncmp(location, "coin:scene.", 11) == 0) {
    // FIXME: implement proper action
  }
  if (strncmp(location, "coin:global.", 12) == 0) {
    // FIXME: implement proper action
  }
  return NULL;
}

/*!
  Clears out all the temporary variables that have the same lifespan as the processing
  of one event.
*/
void
ScXMLCoinEvaluator::clearTemporaryVariables(void)
{
  PRIVATE(this)->clear();
}

void
ScXMLCoinEvaluator::dumpTemporaries(void)
{
  PRIVATE(this)->dump();
}

#undef PRIVATE

// *************************************************************************

SCXML_OBJECT_SOURCE(ScXMLCoinEqualsOpExprDataObj);

void
ScXMLCoinEqualsOpExprDataObj::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLCoinEqualsOpExprDataObj, ScXMLEqualsOpExprDataObj, "ScXMLEqualsOpExprDataObj");
}

void
ScXMLCoinEqualsOpExprDataObj::cleanClass(void)
{
  ScXMLCoinEqualsOpExprDataObj::classTypeId = SoType::badType();
}

SbBool
ScXMLCoinEqualsOpExprDataObj::evaluateNow(ScXMLStateMachine * sm, ScXMLDataObj * & pointer) const
{
  return inherited::evaluateNow(sm, pointer);
}

// *************************************************************************

SCXML_OBJECT_SOURCE(ScXMLCoinAddOpExprDataObj);

void
ScXMLCoinAddOpExprDataObj::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLCoinAddOpExprDataObj, ScXMLAddOpExprDataObj, "ScXMLAddOpExprDataObj");
}

void
ScXMLCoinAddOpExprDataObj::cleanClass(void)
{
  ScXMLCoinAddOpExprDataObj::classTypeId = SoType::badType();
}

SbBool
ScXMLCoinAddOpExprDataObj::evaluateNow(ScXMLStateMachine * sm, ScXMLDataObj * & pointer) const
{
  return inherited::evaluateNow(sm, pointer);
}

// *************************************************************************

SCXML_OBJECT_SOURCE(ScXMLCoinSubtractOpExprDataObj);

void
ScXMLCoinSubtractOpExprDataObj::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLCoinSubtractOpExprDataObj, ScXMLSubtractOpExprDataObj, "ScXMLSubtractOpExprDataObj");
}

void
ScXMLCoinSubtractOpExprDataObj::cleanClass(void)
{
  ScXMLCoinSubtractOpExprDataObj::classTypeId = SoType::badType();
}

ScXMLDataObj *
ScXMLCoinSubtractOpExprDataObj::createFor(ScXMLDataObj * lhs, ScXMLDataObj * rhs)
{
  if (lhs->isOfType(ScXMLSbDataObj::getClassTypeId()) &&
      rhs->isOfType(ScXMLSbDataObj::getClassTypeId())) {
    ScXMLSbDataObj * sblhs = static_cast<ScXMLSbDataObj *>(lhs);
    ScXMLSbDataObj * sbrhs = static_cast<ScXMLSbDataObj *>(rhs);
    SbString lhsstr = sblhs->getSbValue();
    SbString rhsstr = sbrhs->getSbValue();
    if (SbStringConvert::typeOf(lhsstr) == SbStringConvert::typeOf(rhsstr)) {
      switch (SbStringConvert::typeOf(lhsstr)) {
      case SbStringConvert::SBVEC2F:
        {
          SbVec2f lhsvec, rhsvec;
          SbString sbdata;
          // FIXME:
          delete lhs;
          delete rhs;
          return new ScXMLSbDataObj(sbdata.getString());
        }
        break;
      case SbStringConvert::SBVEC3F:
        break;
      default:
        break;
      }
    }
  }
  return ScXMLSubtractOpExprDataObj::createFor(lhs, rhs);
}

SbBool
ScXMLCoinSubtractOpExprDataObj::evaluateNow(ScXMLStateMachine * sm, ScXMLDataObj * & pointer) const
{
  return inherited::evaluateNow(sm, pointer);
}

// *************************************************************************

SCXML_OBJECT_SOURCE(ScXMLCoinMultiplyOpExprDataObj);

void
ScXMLCoinMultiplyOpExprDataObj::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLCoinMultiplyOpExprDataObj, ScXMLMultiplyOpExprDataObj, "ScXMLMultiplyOpExprDataObj");
}

void
ScXMLCoinMultiplyOpExprDataObj::cleanClass(void)
{
  ScXMLCoinMultiplyOpExprDataObj::classTypeId = SoType::badType();
}

SbBool
ScXMLCoinMultiplyOpExprDataObj::evaluateNow(ScXMLStateMachine * sm, ScXMLDataObj * & pointer) const
{
  return inherited::evaluateNow(sm, pointer);
}

// *************************************************************************

SCXML_OBJECT_SOURCE(ScXMLCoinDivideOpExprDataObj);

void
ScXMLCoinDivideOpExprDataObj::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLCoinDivideOpExprDataObj, ScXMLDivideOpExprDataObj, "ScXMLDivideOpExprDataObj");
}

void
ScXMLCoinDivideOpExprDataObj::cleanClass(void)
{
  ScXMLCoinDivideOpExprDataObj::classTypeId = SoType::badType();
}

SbBool
ScXMLCoinDivideOpExprDataObj::evaluateNow(ScXMLStateMachine * sm, ScXMLDataObj * & pointer) const
{
  return inherited::evaluateNow(sm, pointer);
}

// *************************************************************************

SCXML_OBJECT_SOURCE(ScXMLCoinLengthFuncExprDataObj);

void
ScXMLCoinLengthFuncExprDataObj::initClass(void)
{
  SCXML_OBJECT_INIT_CLASS(ScXMLCoinLengthFuncExprDataObj, ScXMLExprDataObj, "ScXMLExprDataObj");
}

void
ScXMLCoinLengthFuncExprDataObj::cleanClass(void)
{
  ScXMLCoinLengthFuncExprDataObj::classTypeId = SoType::badType();
}

ScXMLDataObj *
ScXMLCoinLengthFuncExprDataObj::createFor(ScXMLDataObj * obj)
{
  if (obj->isOfType(ScXMLSbDataObj::getClassTypeId())) {
    ScXMLSbDataObj * sbobj = static_cast<ScXMLSbDataObj *>(obj);
    SbString str = sbobj->getSbValue();
    switch (SbStringConvert::typeOf(str)) {
    case SbStringConvert::SBVEC2F:
      {
        SbVec2f vec = FromString<SbVec2f>(str);
        delete obj;
        return new ScXMLRealDataObj(vec.length());
      }
      break;
    case SbStringConvert::SBVEC3F:
      {
        SbVec3f vec = FromString<SbVec3f>(str);
        delete obj;
        return new ScXMLRealDataObj(vec.length());
      }
      break;
    default:
      break;
    }
    return NULL;
  }
  else if (obj->isOfType(ScXMLExprDataObj::getClassTypeId())) {
    return new ScXMLCoinLengthFuncExprDataObj(obj);
  }
  else {
    return NULL;
  }
  return NULL;
}


ScXMLCoinLengthFuncExprDataObj::ScXMLCoinLengthFuncExprDataObj(void)
: expr(NULL)
{
}

ScXMLCoinLengthFuncExprDataObj::ScXMLCoinLengthFuncExprDataObj(ScXMLDataObj * obj)
: expr(obj)
{
}

ScXMLCoinLengthFuncExprDataObj::~ScXMLCoinLengthFuncExprDataObj(void)
{
  delete this->expr;
  this->expr = NULL;
}

void
ScXMLCoinLengthFuncExprDataObj::setExpr(ScXMLDataObj * obj)
{
  delete this->expr;
  this->expr = obj;
}

SbBool
ScXMLCoinLengthFuncExprDataObj::evaluateNow(ScXMLStateMachine * sm, ScXMLDataObj * & pointer) const
{
  assert(this->expr);
  ScXMLSbDataObj * evaled = NULL;
  if (this->expr->isOfType(ScXMLExprDataObj::getClassTypeId())) {
    ScXMLExprDataObj * rhsexpr = static_cast<ScXMLExprDataObj *>(this->expr);
    ScXMLDataObj * rhsevaled = rhsexpr->evaluate(sm);
    if (!rhsevaled) {
      return FALSE;
    }
    if (!rhsevaled->isOfType(ScXMLSbDataObj::getClassTypeId())) {
      return FALSE;
    }
    evaled = static_cast<ScXMLSbDataObj *>(rhsevaled);
  }
  else if (this->expr->isOfType(ScXMLSbDataObj::getClassTypeId())) {
    evaled = static_cast<ScXMLSbDataObj *>(this->expr);
  }
  else {
    sm->queueInternalEvent("error.eval.Length.INVALID_EXPR");
    return FALSE;
  }

  if (!evaled->isOfType(ScXMLSbDataObj::getClassTypeId())) {
    return FALSE;
  }

  ScXMLSbDataObj * sbobj = static_cast<ScXMLSbDataObj *>(evaled);
  SbString str = sbobj->getSbValue();
  switch (SbStringConvert::typeOf(str)) {
  case SbStringConvert::SBVEC2F:
    {
      SbVec2f vec = FromString<SbVec2f>(str);
      pointer = new ScXMLRealDataObj(vec.length());
      return TRUE;
    }
    break;
  case SbStringConvert::SBVEC3F:
    {
      SbVec3f vec = FromString<SbVec3f>(str);
      pointer = new ScXMLRealDataObj(vec.length());
      return TRUE;
    }
    break;
  default:
    break;
  }

  return FALSE;
}

#ifdef COIN_TEST_SUITE

// FIXME: this is no longer possible in the MinimumEvaluator - move testcase to
// the CoinEvaluator.

#include <cmath>
#include <cfloat>
#include <cstring>

#include <Inventor/SbString.h>
#include <Inventor/scxml/ScXMLStateMachine.h>

template<class EXPECTED_TYPE>
struct DataObjDemangler {
};

template<>
struct DataObjDemangler<ScXMLRealDataObj> {
  typedef double RET_VAL;
  static RET_VAL getValue(ScXMLRealDataObj * obj) {
    return obj->getReal();
  }
};

template<>
struct DataObjDemangler<ScXMLBoolDataObj> {
  typedef bool RET_VAL;
  static RET_VAL getValue(ScXMLBoolDataObj * obj) {
    return obj->getBool();
  }
};

#define COIN_REQUIRE_MESSAGE( P , M) \
  BOOST_REQUIRE_MESSAGE(P , M);      \
  if (!(P))                          \
    return false;


template <class EXPECTED_TYPE>
bool
TestReturnValue(const std::string & evaluationString, typename DataObjDemangler< EXPECTED_TYPE >::RET_VAL retVal, ScXMLEvaluator * evaluator)
{
  ScXMLDataObj * res = NULL;

  res = evaluator->evaluate(evaluationString.c_str());
  COIN_REQUIRE_MESSAGE(res != NULL, std::string("Evaluation returned nothing for expression: ") + evaluationString);
  //FIXME: Should really remember to delete res before returning from this point on,
  //but don't bother about a memory leak when there are bigger fish to
  //fry. 20090613 BFG
  COIN_REQUIRE_MESSAGE(res->getTypeId() == EXPECTED_TYPE::getClassTypeId(), std::string("The type returned was incorrect for expression: ") + evaluationString);
  EXPECTED_TYPE * obj = static_cast<EXPECTED_TYPE *>(res);
  COIN_REQUIRE_MESSAGE(DataObjDemangler<EXPECTED_TYPE>::getValue(obj) == retVal, std::string("Return value mismatch for expression: ") + evaluationString);

  delete res;
  return true;
}


BOOST_AUTO_TEST_CASE(BasicExpressions)
{
  std::unique_ptr<ScXMLStateMachine> sm(new ScXMLStateMachine);
  std::unique_ptr<ScXMLEvaluator> evaluator(new ScXMLCoinEvaluator);
  evaluator->setStateMachine(sm.get());

  TestReturnValue<ScXMLRealDataObj>("M_PI",M_PI,evaluator.get());
  TestReturnValue<ScXMLRealDataObj>("5.0",5.0,evaluator.get());
  TestReturnValue<ScXMLRealDataObj>("5 + 5",10.0,evaluator.get());
  TestReturnValue<ScXMLRealDataObj>("5 - 4",1.0,evaluator.get());
  TestReturnValue<ScXMLRealDataObj>("5 * 4",20.0,evaluator.get());
  TestReturnValue<ScXMLRealDataObj>("5 / 4",1.25,evaluator.get());
  TestReturnValue<ScXMLRealDataObj>("-5",-5.0,evaluator.get());

  TestReturnValue<ScXMLBoolDataObj>("True",TRUE,evaluator.get());
  TestReturnValue<ScXMLBoolDataObj>("false",FALSE,evaluator.get());
  TestReturnValue<ScXMLBoolDataObj>("!false",TRUE,evaluator.get());
  TestReturnValue<ScXMLBoolDataObj>("M_PI != M_LN2",TRUE,evaluator.get());
  TestReturnValue<ScXMLBoolDataObj>("M_PI == M_LN2",FALSE,evaluator.get());
  TestReturnValue<ScXMLBoolDataObj>("false && !false",FALSE,evaluator.get());
}

BOOST_AUTO_TEST_CASE(TemporaryStoragePromotion)
{
  ScXMLStateMachine sm;
  ScXMLCoinEvaluator evaluator;
  evaluator.setStateMachine(&sm);

  for (int i = 0; i < 8; ++i) {
    SbString location;
    location.sprintf("coin:temp.k%d", i);
    SbString value;
    value.sprintf("value%d", i);
    ScXMLStringDataObj input(value.getString());
    BOOST_REQUIRE(evaluator.setAtLocation(location.getString(), &input));
  }

  for (int i = 0; i < 8; ++i) {
    SbString location;
    location.sprintf("coin:temp.k%d", i);
    ScXMLDataObj * result = evaluator.locate(location.getString());
    BOOST_REQUIRE(result != NULL);
    BOOST_REQUIRE(result->isOfType(ScXMLStringDataObj::getClassTypeId()));
    SbString expected;
    expected.sprintf("value%d", i);
    BOOST_CHECK(strcmp(static_cast<ScXMLStringDataObj *>(result)->getString(),
                       expected.getString()) == 0);
  }

  // Replacing a value with itself must clone it before deleting the old value.
  ScXMLDataObj * alias = evaluator.locate("coin:temp.k0");
  BOOST_REQUIRE(alias != NULL);
  BOOST_REQUIRE(evaluator.setAtLocation("coin:temp.k0", alias));
  ScXMLDataObj * replacement = evaluator.locate("coin:temp.k0");
  BOOST_REQUIRE(replacement != NULL);
  BOOST_CHECK(strcmp(static_cast<ScXMLStringDataObj *>(replacement)->getString(),
                     "value0") == 0);

  evaluator.clearTemporaryVariables();
  for (int i = 0; i < 8; ++i) {
    SbString location;
    location.sprintf("coin:temp.k%d", i);
    BOOST_CHECK(evaluator.locate(location.getString()) == NULL);
  }

  // A new event starts in compact mode, then can promote the retained hash again.
  for (int i = 0; i < 8; ++i) {
    SbString location;
    location.sprintf("coin:temp.next%d", i);
    ScXMLStringDataObj input("next");
    BOOST_REQUIRE(evaluator.setAtLocation(location.getString(), &input));
  }
  BOOST_CHECK(evaluator.locate("coin:temp.next0") != NULL);
  BOOST_CHECK(evaluator.locate("coin:temp.next7") != NULL);

  evaluator.clearTemporaryVariables();
  evaluator.setStateMachine(NULL);
}

#endif // !COIN_TEST_SUITE
