/* ================================================================== *
 * Node
 * ================================================================== */

#include "mint/graph/Literal.h"
#include "mint/graph/Node.h"
#include "mint/graph/String.h"

#include "mint/intrinsic/TypeRegistry.h"

#include "mint/support/Diagnostics.h"
#include "mint/support/OStream.h"

namespace mint {

#ifdef NODE_KIND
#undef NODE_KIND
#endif

#define NODE_KIND(x) #x,
#define NODE_KIND_RANGE(x, start, end)

static const char * nodeKindNames[] = {
#include "mint/graph/NodeKind.def"
};

#undef NODE_KIND

Node Node::UNDEFINED_NODE(Node::NK_UNDEFINED, Location(), &TypeRegistry::UNDEFINED_TYPE);

bool isConstant(Node::NodeKind nk) {
  return nk >= Node::NK_CONSTANTS_FIRST && nk <= Node::NK_CONSTANTS_LAST;
}

const char * Node::kindName(NodeKind kind) {
  if (unsigned(kind) < unsigned(sizeof(nodeKindNames) / sizeof(nodeKindNames[0]))) {
    return nodeKindNames[unsigned(kind)];
  }
  return "<Invalid Node Kind>";
}

bool Node::isUndefined() const { return _nodeKind == Node::NK_UNDEFINED; }

Node * Node::getAttributeValue(StringRef name) const {
  if (_type != NULL) {
    return _type->getAttributeValue(name);
  }
  return NULL;
}

bool Node::getAttribute(StringRef name, AttributeLookup & result) const {
  if (_type != NULL) {
    return _type->getAttribute(name, result);
  }
  return false;
}

Node * Node::getElement(Node * index) const {
  diag::error(index->location()) << "Type does not support element access: " << type();
  return &Node::UNDEFINED_NODE;
}

void Node::print(OStream & strm) const {
  switch (_nodeKind) {
    case NK_BOOL: {
      strm << (static_cast<const Literal<bool> *>(this)->value() ? "true" : "false");
      break;
    }

    case NK_INTEGER: {
      strm << static_cast<const Literal<long> *>(this)->value();
      break;
    }

    case NK_FLOAT: {
      strm << static_cast<const Literal<double> *>(this)->value();
      break;
    }

    case NK_UNDEFINED: {
      strm << "undefined";
      break;
    }

    default:
      strm << kindName(_nodeKind);
      break;
  }
}

Node * Node::boolTrue() {
  static GCPointerRoot<Literal<bool> > value =
      new Literal<bool>(Node::NK_BOOL, Location(), TypeRegistry::boolType(), true);
  return value;
}

Node * Node::boolFalse() {
  static GCPointerRoot<Literal<bool> > value =
      new Literal<bool>(Node::NK_BOOL, Location(), TypeRegistry::boolType(), false);
  return value;
}

Node * Node::makeInt(Location loc, int value) {
  return new Literal<int>(Node::NK_INTEGER, loc, TypeRegistry::integerType(), value);
}

Node * Node::makeFloat(Location loc, double value) {
  return new Literal<double>(Node::NK_FLOAT, loc, TypeRegistry::floatType(), value);
}

void Node::dump() const {
  this->print(console::err());
  console::err() << "\n";
}

void Node::trace() const {
  _location.trace();
  safeMark(_type);
}

/// Return true if this node kind represents a constant.
bool Node::isConstant(NodeKind nk) {
  return nk >= Node::NK_CONSTANTS_FIRST && nk <= Node::NK_CONSTANTS_LAST;
}

OStream & operator<<(OStream & strm, Node::NodeKind nk) {
  strm << Node::kindName(nk);
  return strm;
}

}
