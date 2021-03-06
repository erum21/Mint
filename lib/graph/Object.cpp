/* ================================================================== *
 * Object
 * ================================================================== */

#include "mint/intrinsic/StringRegistry.h"
#include "mint/intrinsic/TypeRegistry.h"

#include "mint/graph/GraphWriter.h"
#include "mint/graph/Object.h"
#include "mint/graph/Oper.h"

#include "mint/support/Assert.h"
#include "mint/support/Diagnostics.h"
#include "mint/support/OStream.h"

namespace mint {

// -------------------------------------------------------------------------
// Object
// -------------------------------------------------------------------------

void AttributeDefinition::print(OStream & strm) const {
  strm << "param :" << type() << " = " << _value;
}

// -------------------------------------------------------------------------
// Object
// -------------------------------------------------------------------------

// TODO: Deprecate this and use type only.
Object * Object::prototype() const {
  return type() && type()->nodeKind() == NK_OBJECT ? static_cast<Object *>(type()) : NULL;
}

bool Object::inheritsFrom(Object * proto) const {
  for (const Node * o = this; o != NULL; o = o->type()) {
    if (o == proto) {
      return true;
    }
  }
  return false;
}

void Object::setName(StringRef name) {
  setName(StringRegistry::str(name));
}

AttributeDefinition * Object::defineAttribute(StringRef name, Node * value, Type * type, int flags) {
  AttributeDefinition * p = new AttributeDefinition(value, type, flags);
  _attrs[strings::str(name)] = p;
  return p;
}

AttributeDefinition * Object::defineDynamicAttribute(
    StringRef name, Type * type, MethodHandler * mh, int flags) {
  TypeRegistry & typeReg = TypeRegistry::get();
  String * attrName = StringRegistry::str(name);
  DerivedType * functionType = typeReg.getFunctionType(type);
  Function * func = new Function(Node::NK_FUNCTION, Location(), functionType, mh);
  func->setName(attrName);
  func->setParentScope(this);
  Node *args[] = { func, this };
  Node * call = Oper::create(Node::NK_DEFERRED, Location(), type, args);
  AttributeDefinition * attrDef = new AttributeDefinition(call, type, flags);
  _attrs[attrName] = attrDef;
  return attrDef;
}

Node * Object::getAttributeValue(StringRef name) const {
  for (const Node * ob = this; ob != NULL; ob = ob->type()) {
    if (ob->nodeKind() >= Node::NK_OBJECTS_FIRST && ob->nodeKind() <= Node::NK_OBJECTS_LAST) {
      const Attributes & attrs = static_cast<const Object *>(ob)->_attrs;
      Attributes::const_iterator it = attrs.find_as(name);
      if (it != attrs.end()) {
        return it->second;
      }
    }
  }
  return NULL;
}

bool Object::getAttribute(StringRef name, AttributeLookup & result) const {
  for (const Node * ob = this; ob != NULL; ob = ob->type()) {
    if (ob->nodeKind() >= Node::NK_OBJECTS_FIRST && ob->nodeKind() <= Node::NK_OBJECTS_LAST) {
      const Attributes & attrs = static_cast<const Object *>(ob)->_attrs;
      Attributes::const_iterator it = attrs.find_as(name);
      if (it != attrs.end()) {
        Node * n = it->second;
        if (n->nodeKind() == Node::NK_ATTRDEF) {
          result.definition = static_cast<AttributeDefinition *>(n);
          if (result.value == NULL) {
            result.value = result.definition->value();
            result.foundScope = const_cast<Node *>(ob);
          }
          break;
        } else if (result.value == NULL) {
          result.foundScope = const_cast<Node *>(ob);
          result.value = n;
        }
      }
    }
  }
  return result.value != NULL;
}

Node * Object::getElement(Node * index) const {
  if (String * str = String::dyn_cast(index)) {
    return getAttributeValue(str->value());
  }
  diag::error(index->location()) << "Invalid key type: " << index->type();
  return &Node::UNDEFINED_NODE;
}

Function * Object::defineMethod(StringRef name, StringRef signature, MethodHandler * m) {
  SmallVector<Type *, 16> argTypes;
  SmallVector<Parameter, 16> params;
  TypeRegistry & tr = TypeRegistry::get();

  Attributes::const_iterator it = _attrs.find_as(name);
  if (it != _attrs.end()) {
    diag::error() << "Method '" << name << "' is already defined on '" << this << "'";
  }

  size_t pos = 0;
  Type * returnType = tr.parseTypeCode(signature, pos);
  M_ASSERT(returnType != NULL);

  while (pos < signature.size()) {
    if (signature[pos] != ',') {
      break;
    }
    ++pos;
    M_ASSERT(pos < signature.size());
    size_t colon = signature.find(':', pos);
    M_ASSERT(pos != signature.npos);
    Parameter param(String::create(signature.substr(pos, colon - pos)));
    pos = colon+1;
    M_ASSERT(pos < signature.size());
    char ch = signature[pos];
    if (ch == '*') {
      param.setVariadic(true);
      ++pos;
    }
    Type * paramType = tr.parseTypeCode(signature, pos);
    M_ASSERT(paramType != NULL);
    argTypes.push_back(paramType);
    params.push_back(param);
  }

  M_ASSERT(pos == signature.size());
  M_ASSERT(m != NULL);
  DerivedType * functionType = TypeRegistry::get().getFunctionType(returnType, argTypes);
  Function * method = new Function(Node::NK_FUNCTION, Location(), functionType, params, m);
  String * methodName = StringRegistry::str(name);
  method->setName(methodName);
  method->setParentScope(this);
  _attrs[methodName] = method;
  return method;
}

Function * Object::defineMethod(StringRef name, Type * returnType, MethodHandler * m) {
  return defineMethod(name, returnType, TypeArray(), m);
}

Function * Object::defineMethod(StringRef name, Type * returnType, Type * a0, MethodHandler * m) {
  return defineMethod(name, returnType, makeArrayRef(a0), m);
}

Function * Object::defineMethod(
    StringRef name, Type * returnType, Type * a0, Type * a1, MethodHandler * m) {
  Type * args[] = { a0, a1 };
  return defineMethod(name, returnType, args, m);
}

Function * Object::defineMethod(
    StringRef name, Type * returnType, Type * a0, Type * a1, Type * a2, MethodHandler * m) {
  Type * args[] = { a0, a1, a2 };
  return defineMethod(name, returnType, args, m);
}

Function * Object::defineMethod(
    StringRef name, Type * returnType, TypeArray args, MethodHandler * m) {
  M_ASSERT(returnType != NULL);
  M_ASSERT(m != NULL);
  DerivedType * functionType = TypeRegistry::get().getFunctionType(returnType, args);
  Function * method = new Function(Node::NK_FUNCTION, Location(), functionType, m);
  Attributes::const_iterator it = _attrs.find_as(name);
  if (it != _attrs.end()) {
    diag::error() << "Method '" << name << "' is already defined on '" << this << "'";
  }
  String * methodName = StringRegistry::str(name);
  method->setName(methodName);
  method->setParentScope(this);
  _attrs[methodName] = method;
  return method;
}

Object * Object::makeDict(Object * prototype, StringRef name) {
  Object * result = new Object(NK_DICT, Location(), prototype);
  if (!name.empty()) {
    result->setName(name);
  }
  return result;
}

void Object::print(OStream & strm) const {
  if (name()) {
    strm << name();
  } else if (prototype()) {
    strm << prototype();
  } else {
    strm << "<object>";
  }
}

void Object::dump() const {
  GraphWriter writer(console::err());
  writer.write(const_cast<Object *>(this), true);
  console::err() << "\n";
}

void Object::trace() const {
  Node::trace();
  safeMark(_definition);
  safeMark(_name);
  safeMark(_parentScope);
  _attrs.trace();
}

}
