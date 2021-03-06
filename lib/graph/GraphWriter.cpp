/* ================================================================== *
 * Mint: A refreshing approach to build configuration.
 * ================================================================== */

#include "mint/graph/GraphWriter.h"
#include "mint/graph/Module.h"
#include "mint/graph/Object.h"
#include "mint/graph/Oper.h"
#include "mint/graph/Type.h"
#include "mint/graph/String.h"

#include "mint/intrinsic/TypeRegistry.h"

#include "mint/support/Assert.h"
#include "mint/support/Diagnostics.h"
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
  writeValue(node, isDefinition);
  return *this;
}

GraphWriter & GraphWriter::write(Module * module) {
  _strm << "module(" << module->name() << ") {\n";
  ++_indentLevel;
  Node * savedScope = setActiveScope(module);
  _activeModule = module;
  for (SmallVectorImpl<String *>::const_iterator
      it = module->keyOrder().begin(), itEnd = module->keyOrder().end(); it != itEnd; ++it) {
    Node * n = module->attrs()[*it];
    if (filter(n)) {
      _strm.indent(_indentLevel * 2);
      _strm << *it << " = ";
      write(n, true);
      _strm << "\n";
    }
  }
  setActiveScope(savedScope);
  _activeModule = NULL;
  --_indentLevel;
  _strm.indent(_indentLevel * 2);
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

GraphWriter & GraphWriter::writeCachedVars(Module * module) {
  ++_indentLevel;
  _strm.indent(_indentLevel * 2);
  _strm << "cached_vars = object {\n";
  ++_indentLevel;
  Node * savedScope = setActiveScope(module);
  _activeModule = module;
  for (SmallVectorImpl<String *>::const_iterator
      it = module->keyOrder().begin(), itEnd = module->keyOrder().end(); it != itEnd; ++it) {
    Node * n = module->attrs()[*it];
    if (filter(n)) {
      writeCachedVars(module, *it, n);
    }
  }
  setActiveScope(savedScope);
  _activeModule = NULL;
  --_indentLevel;
  _strm.indent(_indentLevel * 2);
  _strm << "}\n";
  --_indentLevel;
  return *this;
}

void GraphWriter::writeCachedVars(Node * scope, String * name, Node * value) {
  AttributeLookup lookup;
  if (scope->getAttribute(name->value(), lookup) &&
      lookup.definition != NULL &&
      lookup.definition->isCached()) {
    _strm.indent(_indentLevel * 2);
    writeRelativePath(scope);
    _strm << name;
    writeAssignedValue(value);
    _strm << "\n";
  } else {
    Object * obj = value->asObject();
    if (obj != NULL && !obj->attrs().empty()) {
      SmallVector<Attributes::value_type, 64 > objectProperties;
      objectProperties.resize(obj->attrs().size());
      std::copy(obj->attrs().begin(), obj->attrs().end(), objectProperties.begin());
      std::sort(objectProperties.begin(), objectProperties.end(), StringDictComparator());
      for (SmallVectorImpl<Attributes::value_type>::const_iterator
          it = objectProperties.begin(), itEnd = objectProperties.end(); it != itEnd; ++it) {
        if (filter(it->second)) {
          writeCachedVars(obj, it->first, it->second);
        }
      }
    }
  }
}

bool GraphWriter::writeValue(Node * node, bool isDefinition) {
  switch (node->nodeKind()) {
    case Node::NK_UNDEFINED:
      break;

    case Node::NK_BOOL:
    case Node::NK_INTEGER:
    case Node::NK_FLOAT:
    case Node::NK_STRING:
    case Node::NK_TYPENAME:
    case Node::NK_IDENT:
      _strm << node;
      return true;

    case Node::NK_LIST:
      writeList(static_cast<Oper *>(node));
      return true;

    case Node::NK_DICT:
      writeDict(static_cast<Object *>(node));
      return true;

    case Node::NK_FUNCTION:
      return false;

    case Node::NK_OBJECT:
      return writeObject(static_cast<Object *>(node), isDefinition);

    case Node::NK_ACTION_COMMAND: {
      Oper * op = static_cast<Oper *>(node);
      _strm << "fundamentals.command(";
      writeValue(op->arg(0));
      _strm << ",";
      writeValue(op->arg(1));
      _strm << ")";
      break;
    }

    case Node::NK_ACTION_MESSAGE: {
      Oper * op = static_cast<Oper *>(node);
      _strm << "fundamentals.message."
          << diag::severityMethodName(diag::Severity(op->arg(0)->requireInt()))
          << "(";
      writeValue(op->arg(1));
      _strm << ")";
      break;
    }

    default:
      console::err() << "Invalid node type for writing: " << node->nodeKind() << "\n";
      console::err() << "Node value is: " << node << "\n";
      return false;
  }
  return false;
}

void GraphWriter::writeList(Oper * list) {
  if (list->size() == 0) {
    _strm << "[]";
    return;
  }
  _strm << "[\n";
  ++_indentLevel;
  for (Oper::const_iterator it = list->begin(), itEnd = list->end(); it != itEnd; ++it) {
    Node * n = *it;
    if (filter(n)) {
      if (it != list->begin()) {
        _strm << ",\n";
      }
      _strm.indent(_indentLevel * 2);
      write(n, false);
    }
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

bool GraphWriter::writeObject(Object * obj, bool isDefinition) {
  // Write a reference to the object instead of the literal body
  if (!isDefinition && hasRelativePath(obj)) {
    if (obj->parentScope()) {
      _strm << "\"";
      writeRelativePath(obj->parentScope());
      _strm << obj->name();
      _strm << "\"";
    } else {
      _strm << obj->name();
    }
    return true;
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
    return true;
  }
  _strm << " {\n";
  ++_indentLevel;
  Node * savedScope = setActiveScope(obj);
  writeObjectContents(obj);
  setActiveScope(savedScope);
  --_indentLevel;
  _strm.indent(_indentLevel * 2);
  _strm << "}";
  return true;
}

void GraphWriter::writeObjectContents(Object * obj) {
  SmallVector<Attributes::value_type, 64 > objectProperties;
  objectProperties.resize(obj->attrs().size());
  std::copy(obj->attrs().begin(), obj->attrs().end(), objectProperties.begin());
  std::sort(objectProperties.begin(), objectProperties.end(), StringDictComparator());
  for (SmallVectorImpl<Attributes::value_type>::const_iterator
      it = objectProperties.begin(), itEnd = objectProperties.end(); it != itEnd; ++it) {
    if (!filter(it->second)) {
      continue;
    }
    if (it->second->nodeKind() == Node::NK_UNDEFINED || it->second->nodeKind() == Node::NK_MODULE) {
      // TODO: This is kind of a hack, figure a better way to suppress unimportant attrs.
      continue;
    } else if (it->second->nodeKind() == Node::NK_ATTRDEF) {
      AttributeDefinition * attrDef = static_cast<AttributeDefinition *>(it->second);
      _strm.indent(_indentLevel * 2);
      _strm << "param " << it->first << " : " << attrDef->type();
      writeAssignedValue(attrDef->value());
      _strm << "\n";
    } else {
      _strm.indent(_indentLevel * 2);
      _strm << it->first;
      writeAssignedValue(it->second);
      _strm << "\n";
    }
  }
}

void GraphWriter::writeAssignedValue(Node * n) {
  if (n->nodeKind() == Node::NK_DEFERRED) {
    Oper * op = static_cast<Oper *>(n);
    _strm << " => ";
    if (op->arg(0)->nodeKind() == Node::NK_FUNCTION) {
      Function * fn = static_cast<Function *>(op->arg(0));
      if (fn->name() != NULL) {
        _strm << fn->name();
      } else if (fn->body() != NULL) {
        write(fn->body(), false);
      } else {
        _strm << "<unnamed function>";
      }
    } else {
      write(op->arg(0), false);
    }
  } else {
    _strm << " = ";
    write(n, false);
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
      _strm << "#";
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

bool GraphWriter::filter(Node * n) {
  if (!_includeOptions) {
    Object * obj = n->asObject();
    if (obj != NULL && obj->inheritsFrom(TypeRegistry::optionType())) {
      return false;
    }
  }
  if (!_includeTargets) {
    Object * obj = n->asObject();
    if (obj != NULL && obj->inheritsFrom(TypeRegistry::targetType())) {
      return false;
    }
  }
  return true;
}

}
