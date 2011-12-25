/* ================================================================== *
 * Fundamentals
 * ================================================================== */

#include "mint/eval/Evaluator.h"

#include "mint/intrinsic/Fundamentals.h"
#include "mint/intrinsic/StringRegistry.h"

#include "mint/graph/GraphBuilder.h"
#include "mint/graph/Object.h"
#include "mint/graph/Oper.h"

#include "mint/support/Assert.h"
#include "mint/support/Diagnostics.h"
#include "mint/support/OStream.h"
#include "mint/support/Path.h"

namespace mint {

Node * methodObjectPrototype(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  return static_cast<Object *>(self)->prototype();
}

Node * methodObjectName(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  return static_cast<Object *>(self)->name();
}

Node * methodObjectParent(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  return self->parentScope();
}

Node * methodObjectModule(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  Node * module = self->module();
  return module != NULL ? module : &Node::UNDEFINED_NODE;
}

Node * methodObjectCompose(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 1);
  Oper * list = args[0]->asOper();
  M_ASSERT(list != NULL);
  Object * result = new Object(loc, self->asObject(), NULL);
  result->setParentScope(ex->lexicalScope());
  ex->copyParams(result, list);
  return result;
}

Node * methodTargetForSource(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 2);
  String * source = args[0]->requireString();
  Oper * params = args[1]->requireOper();

  M_ASSERT(path::isAbsolute(source->value())) << "Path must be absolute: " << source;
  Object * result = new Object(loc, self->asObject(), NULL);
  result->setParentScope(ex->lexicalScope());
  result->setAttribute(
      strings::str("sources"),
      Oper::createList(source->location(), TypeRegistry::stringListType(), source));
  ex->copyParams(result, params);
  return result;
}

Node * methodTargetForOutput(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 2);
  String * output = args[0]->requireString();
  Oper * params = args[1]->requireOper();

  M_ASSERT(path::isAbsolute(output->value())) << "Path must be absolute: " << output;
  Object * result = new Object(loc, self->asObject(), NULL);
  result->setParentScope(ex->lexicalScope());
  result->setAttribute(
      strings::str("outputs"),
      Oper::createList(output->location(), TypeRegistry::stringListType(), output));
  ex->copyParams(result, params);
  return result;
}

Node * functionRequire(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 1);
  if (!ex->isNonNil(args[0])) {
    diag::error(loc) << "Missing required value";
  }
  return &Node::UNDEFINED_NODE;
}

Node * functionCommand(Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 2);
  M_ASSERT(args[0]->nodeKind() == Node::NK_STRING);
  M_ASSERT(args[1]->nodeKind() == Node::NK_LIST);
  return Oper::create(Node::NK_ACTION_COMMAND, loc, TypeRegistry::actionType(), args);
}

//Node * functionCaller(
//    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
//  return ex->caller(loc, 4);
//}

Fundamentals::Fundamentals() : Module("<fundamentals>", NULL) {
  Location loc;

  // Listed in it's own namespace
  setAttribute(str("fundamentals"), this);

  // Initialize all of the built-in types

  initObjectType();
  initTargetType();
  initOptionType();
  initModuleType();
  initActionType();
  initStringType();
  initListType();

  // Built-in namespaces

  initPlatformVars();

  // Built-in methods that are in specific namespaces

  initConsoleMethods(this);
  initPathMethods(this);
  initRegExMethods(this);

  // Built-in methods that are in the global namespace

  initSubprocessMethods(this);
  initFileMethods(this);
  initDirSearchMethods(this);

  // Built-in global methods
  defineMethod("require", TypeRegistry::anyType(), TypeRegistry::anyType(), functionRequire);
  defineMethod("command", TypeRegistry::actionType(), TypeRegistry::stringType(),
      TypeRegistry::stringListType(), functionCommand);
  //defineDynamicAttribute("caller", TypeRegistry::objectType(), functionCaller, 0);
}

void Fundamentals::initObjectType() {
  // Type 'object'
  Object * objectType = TypeRegistry::objectType();
  setAttribute(objectType->name(), objectType);
  if (objectType->attrs().empty()) {
    Type * typeObjectList = TypeRegistry::get().getListType(TypeRegistry::objectType());

    objectType->defineDynamicAttribute("prototype", objectType, methodObjectPrototype, 0);
    objectType->defineDynamicAttribute("name", TypeRegistry::stringType(), methodObjectName, 0);
    objectType->defineDynamicAttribute("module", TypeRegistry::moduleType(), methodObjectModule, 0);
    objectType->defineDynamicAttribute("parent", TypeRegistry::objectType(), methodObjectParent, 0);
    objectType->defineMethod(
        "compose", TypeRegistry::objectType(), typeObjectList, methodObjectCompose);
  }
}

void Fundamentals::initTargetType() {
  GraphBuilder builder;

  // Type 'target'
  Object * targetType = TypeRegistry::targetType();
  setAttribute(targetType->name(), targetType);
  if (targetType->attrs().empty()) {
    targetType->setType(TypeRegistry::objectType());
    Type * typeObjectList = TypeRegistry::get().getListType(TypeRegistry::objectType());

    // Create a type that is a list of files (strings?)
    Node * stringListEmpty = builder.createListOf(Location(), TypeRegistry::stringType());
    targetType->defineAttribute("sources", stringListEmpty, TypeRegistry::stringListType(),
        AttributeDefinition::CACHED | AttributeDefinition::PARAM);
    targetType->defineAttribute("outputs", stringListEmpty, TypeRegistry::stringListType(),
        AttributeDefinition::CACHED | AttributeDefinition::PARAM);

    // Create a type that is a list of targets.
    Node * targetListEmpty = builder.createListOf(Location(), targetType);
    targetType->defineAttribute(
        "depends", targetListEmpty, targetListEmpty->type(),
        AttributeDefinition::CACHED | AttributeDefinition::PARAM);
    targetType->defineAttribute(
        "implicit_depends", targetListEmpty, targetListEmpty->type(),
        AttributeDefinition::CACHED);
    targetType->defineMethod(
        "for_source", TypeRegistry::targetType(), TypeRegistry::stringType(), typeObjectList,
        methodTargetForSource);
    targetType->defineMethod(
        "for_output", TypeRegistry::targetType(), TypeRegistry::stringType(), typeObjectList,
        methodTargetForOutput);
  }
}

void Fundamentals::initOptionType() {
  Object * optionType = TypeRegistry::optionType();
  setAttribute(optionType->name(), optionType);
  if (optionType->attrs().empty()) {
    optionType->setName("option");
    optionType->defineDynamicAttribute("name", TypeRegistry::stringType(), methodObjectName);
    optionType->defineAttribute("help", &Node::UNDEFINED_NODE, TypeRegistry::stringType());
    optionType->defineAttribute("abbrev", &Node::UNDEFINED_NODE, TypeRegistry::stringType());
    optionType->defineAttribute("group", &Node::UNDEFINED_NODE, TypeRegistry::stringType());
  }
}

void Fundamentals::initActionType() {
  // Type 'action'
  Object * actionType = TypeRegistry::actionType();
  setAttribute(actionType->name(), actionType);
  if (actionType->attrs().empty()) {
    //Type * typeObjectList = TypeRegistry::get().getListType(TypeRegistry::actionType());
  }
}

Node * methodStringSize(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 0);
  String * selfStr = self->requireString();
  return Node::makeInt(loc, selfStr->size());
}

Node * methodStringSubstr(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 2);
  String * selfStr = self->requireString();
  int start = args[0]->requireInt();
  int length = args[1]->requireInt();
  return String::create(loc, selfStr->value().substr(start, length));
}

Node * methodStringStartsWith(
    Location loc, Evaluator * ex, Function * fn, Node * self, NodeArray args) {
  M_ASSERT(args.size() == 1);
  String * selfStr = self->requireString();
  String * prefixStr = args[0]->requireString();
  return Node::makeBool(selfStr->value().startsWith(prefixStr->value()));
}

void Fundamentals::initStringType() {
  // Type 'string'
  Object * stringMetaType = TypeRegistry::stringMetaType();
  //setAttribute(stringType->name(), stringType);
  if (stringMetaType->attrs().empty()) {
    TypeRegistry::stringType()->setType(stringMetaType);
    stringMetaType->defineDynamicAttribute("size", TypeRegistry::integerType(), methodStringSize, 0);
    stringMetaType->defineMethod(
        "substr", TypeRegistry::stringType(), TypeRegistry::integerType(),
        TypeRegistry::integerType(), methodStringSubstr);
    stringMetaType->defineMethod("starts_with", TypeRegistry::boolType(), TypeRegistry::stringType(),
        methodStringStartsWith);
  }
}

void Fundamentals::initPlatformVars() {
  Object * platform = createChildScope("platform");
  platform->attrs()[StringRegistry::str(HOST_PLATFORM)] = Node::boolTrue();
}

String * Fundamentals::str(StringRef in) {
  return StringRegistry::str(in);
}

Object * Fundamentals::createChildScope(StringRef name) {
  String * strName = str(name);
  Object * obj = new Object(Node::NK_DICT, Location(), NULL);
  obj->setName(strName);
  setAttribute(strName, obj);
  return obj;
}

Fundamentals & Fundamentals::get() {
  static GCPointerRoot<Fundamentals> instance(new Fundamentals());
  return *instance;
}

void Fundamentals::trace() const {
  Module::trace();
}

}
