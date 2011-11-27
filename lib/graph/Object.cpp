/* ================================================================== *
 * Object
 * ================================================================== */

#include "mint/graph/Object.h"
#include "mint/support/OStream.h"

namespace mint {

// -------------------------------------------------------------------------
// Object
// -------------------------------------------------------------------------

bool Object::inheritsFrom(Object * proto) const {
  for (const Object * o = this; o != NULL; o = o->_prototype) {
    if (o == proto) {
      return true;
    }
  }
  return false;
}

Property * Object::defineProperty(String * name, Node * value, Type * type, unsigned lazy) {
  Property * p = new Property(value, type, lazy);
  _properties[name] = p;
  return p;
}

Node * Object::getPropertyValue(StringRef name) const {
  for (const Object * ob = this; ob != NULL; ob = ob->prototype()) {
    PropertyTable::const_iterator it = ob->_properties.find_as(name);
    if (it != ob->_properties.end()) {
      return it->second;
    }
  }
  return NULL;
}

Property * Object::getPropertyDefinition(StringRef name) const {
  for (const Object * o = this; o != NULL; o = o->prototype()) {
    PropertyTable::const_iterator it = o->_properties.find_as(name);
    if (it != o->_properties.end()) {
      if (it->second->nodeKind() == Node::NK_PROPDEF) {
        return static_cast<Property *>(it->second);
      }
    }
  }
  return NULL;
}

bool Object::hasPropertyImmediate(StringRef name) const {
  return _properties.find_as(name) != _properties.end();
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
  if (this->name() != NULL) {
    console::err() << this->name();
  }
  if (_prototype != NULL) {
    console::err() << " = " << _prototype->name();
  }
  console::err() << " {\n";
  for (PropertyTable::const_iterator it = _properties.begin(), itEnd = _properties.end(); it != itEnd; ++it) {
    console::err() << "  " << it->first << " = " << it->second << "\n";
  }
  console::err() << "}\n";
}

void Object::trace() const {
  Node::trace();
  safeMark(_definition);
  safeMark(_name);
  safeMark(_prototype);
  safeMark(_parentScope);
  _properties.trace();
}

}
