/* ================================================================== *
 * Mint: A refreshing approach to build configuration.
 * ================================================================== */

#include "mint/graph/GraphWriter.h"
#include "mint/graph/Module.h"
#include "mint/graph/Object.h"
#include "mint/graph/Oper.h"
#include "mint/graph/Type.h"
#include "mint/graph/String.h"

#include "mint/support/Assert.h"
#include "mint/support/Path.h"

#if HAVE_ALGORITHM
#include <algorithm>
#endif

namespace mint {

struct StringDictComparator {
  inline bool operator()(
      const StringDict<Node>::value_type & lhs, const StringDict<Node>::value_type & rhs) {
    M_ASSERT(lhs.first != NULL);
    M_ASSERT(rhs.first != NULL);
    return lhs.first->value().compare(rhs.first->value()) < 0;
  }
};

GraphWriter & GraphWriter::write(Node * node, bool isDefinition) {
  switch (node->nodeKind()) {
    case Node::NK_UNDEFINED:
    case Node::NK_IDENT:
      break;

    case Node::NK_BOOL:
    case Node::NK_INTEGER:
    case Node::NK_FLOAT:
    case Node::NK_STRING:
    case Node::NK_TYPENAME:
      _strm << node;
      break;

    case Node::NK_LIST:
      writeList(static_cast<Oper *>(node));
      break;

    case Node::NK_DICT:
      writeDict(static_cast<Object *>(node));
      break;

    case Node::NK_FUNCTION:
      break;

    case Node::NK_OBJECT:
      writeObject(static_cast<Object *>(node), isDefinition);
      break;

    default:
      console::err() << "Invalid node type for writing: " << node->nodeKind() << "\n";
      console::err() << "Node value is: " << node << "\n";
      break;
  }
  return *this;
}

GraphWriter & GraphWriter::write(Module * module) {
  _strm << "module " << module->name() << " {\n";
  ++_indentLevel;
  Node * savedScope = setActiveScope(module);
  _activeModule = module;
  for (SmallVectorImpl<String *>::const_iterator
      it = module->keyOrder().begin(), itEnd = module->keyOrder().end(); it != itEnd; ++it) {
    _strm.indent(_indentLevel * 2);
    _strm << *it << " = ";
    write(module->attrs()[*it], true);
    _strm << "\n";
  }
  setActiveScope(savedScope);
  _activeModule = NULL;
  --_indentLevel;
  _strm << "}\n";
  return *this;
}

GraphWriter & GraphWriter::write(ArrayRef<Node *> nodes, bool isDefinition) {
  for (ArrayRef<Node *>::iterator it = nodes.begin(), itEnd = nodes.end(); it != itEnd; ++it) {
    _strm.indent(_indentLevel * 2);
    write(*it, isDefinition);
    _strm << "\n";
  }
  return *this;
}

void GraphWriter::writeList(Oper * list) {
  if (list->size() == 0) {
    _strm << "[]";
    return;
  }
  _strm << "[\n";
  ++_indentLevel;
  for (Oper::const_iterator it = list->begin(), itEnd = list->end(); it != itEnd; ++it) {
    if (it != list->begin()) {
      _strm << ",\n";
    }
    _strm.indent(_indentLevel * 2);
    write(*it, false);
  }
  --_indentLevel;
  _strm << "\n";
  _strm.indent(_indentLevel * 2);
  _strm << "]";
}

void GraphWriter::writeDict(Object * dict) {
  _strm << "{\n";
  ++_indentLevel;
  SmallVector<Attributes::value_type, 64 > dictProperties;
  dictProperties.resize(dict->attrs().size());
  std::copy(dict->attrs().begin(), dict->attrs().end(), dictProperties.begin());
  std::sort(dictProperties.begin(), dictProperties.end(), StringDictComparator());
  for (SmallVectorImpl<Attributes::value_type>::const_iterator
      it = dictProperties.begin(), itEnd = dictProperties.end(); it != itEnd; ++it) {
    _strm.indent(_indentLevel * 2);
    _strm << it->first << " = ";
    write(it->second, true);
    _strm << "\n";
  }
  --_indentLevel;
  _strm.indent(_indentLevel * 2);
  _strm << "}";
}

void GraphWriter::writeObject(Object * obj, bool isDefinition) {
  // Write a reference to the object instead of the literal body
  if (!isDefinition && hasRelativePath(obj)) {
    if (obj->parentScope()) {
      writeRelativePath(obj->parentScope());
    }
    _strm << obj->name();
    return;
  }

  // Write the proto
  if (obj->prototype() != NULL) {
    if (obj->prototype()->name() != NULL) {
      _strm << obj->prototype()->name();
    } else {
      writeObject(obj->prototype(), false);
    }
  }
  if (obj->attrs().empty()) {
    _strm << " {}";
    return;
  }
  _strm << " {\n";
  ++_indentLevel;
  Node * savedScope = setActiveScope(obj);
  writeObjectContents(obj);
  setActiveScope(savedScope);
  --_indentLevel;
  _strm.indent(_indentLevel * 2);
  _strm << "}";
}

void GraphWriter::writeObjectContents(Object * obj) {
  SmallVector<Attributes::value_type, 64 > objectProperties;
  objectProperties.resize(obj->attrs().size());
  std::copy(obj->attrs().begin(), obj->attrs().end(), objectProperties.begin());
  std::sort(objectProperties.begin(), objectProperties.end(), StringDictComparator());
  for (SmallVectorImpl<Attributes::value_type>::const_iterator
      it = objectProperties.begin(), itEnd = objectProperties.end(); it != itEnd; ++it) {
    if (it->second->nodeKind() == Node::NK_UNDEFINED || it->second->nodeKind() == Node::NK_MODULE) {
      // TODO: This is kind of a hack, figure a better way to suppress unimportant attrs.
      continue;
    } else if (it->second->nodeKind() == Node::NK_PROPDEF) {
      AttributeDefinition * attrDef = static_cast<AttributeDefinition *>(it->second);
      _strm.indent(_indentLevel * 2);
      _strm << "param " << it->first << " : " << attrDef->type() << " = ";
      write(attrDef->value(), false);
      _strm << "\n";
    } else {
      _strm.indent(_indentLevel * 2);
      _strm << it->first << " = ";
      write(it->second, false);
      _strm << "\n";
    }
  }
}

void GraphWriter::writeOptionContents(Object * obj) {
  SmallVector<Attributes::value_type, 64 > objectProperties;
  objectProperties.resize(obj->attrs().size());
  std::copy(obj->attrs().begin(), obj->attrs().end(), objectProperties.begin());
  std::sort(objectProperties.begin(), objectProperties.end(), StringDictComparator());
  for (SmallVectorImpl<Attributes::value_type>::const_iterator
      it = objectProperties.begin(), itEnd = objectProperties.end(); it != itEnd; ++it) {
    if (it->second->nodeKind() == Node::NK_PROPDEF) {

    }
    if (it->second->nodeKind() == Node::NK_UNDEFINED ||
        it->second->nodeKind() == Node::NK_MODULE ||
        it->second->nodeKind() == Node::NK_PROPDEF) {
      // TODO: This is kind of a hack, figure a better way to suppress unimportant attrs.
      continue;
    }
    _strm.indent(_indentLevel * 2);
    _strm << it->first << " = ";
    write(it->second, true);
    _strm << "\n";
  }
}

void GraphWriter::writeRelativePath(Node * scope) {
  M_ASSERT(scope != NULL);
  if (scope == _activeScope) {
    return;
  } else if (scope->nodeKind() == Node::NK_MODULE) {
    if (scope != _activeModule) {
      Module * m = static_cast<Module *>(scope);
      _strm << path::parent(m->name()->value());
    } else {
      _strm << ":";
    }
  } else if (scope->nodeKind() == Node::NK_OBJECT) {
    Object * obj = static_cast<Object *>(scope);
    if (obj->parentScope() != NULL) {
      writeRelativePath(obj->parentScope());
    }
    _strm << obj->name() << ".";
  }
}

bool GraphWriter::hasRelativePath(Object * obj) {
  for (Node * scope = obj; scope != NULL; scope = scope->parentScope()) {
    if (scope == _activeScope) {
      return true;
    } else if (scope->nodeKind() == Node::NK_MODULE) {
      return true;
    } else if (scope->nodeKind() == Node::NK_OBJECT) {
      Object * scopeObj = static_cast<Object *>(scope);
      if (scopeObj->name() == NULL) {
        return false;
      }
    } else {
      return false;
    }
  }
  return false;
}

}
