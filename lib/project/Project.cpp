/* ================================================================== *
 * Project
 * ================================================================== */

#include "mint/build/TargetMgr.h"
#include "mint/build/TargetFinder.h"

#include "mint/eval/Evaluator.h"

#include "mint/graph/Object.h"
#include "mint/graph/GraphWriter.h"

#include "mint/intrinsic/Fundamentals.h"
#include "mint/intrinsic/StringRegistry.h"

#include "mint/project/BuildConfiguration.h"
#include "mint/project/Configurator.h"
#include "mint/project/MakefileGenerator.h"
#include "mint/project/OptionFinder.h"
#include "mint/project/Project.h"

#include "mint/support/Assert.h"
#include "mint/support/CommandLine.h"
#include "mint/support/Diagnostics.h"
#include "mint/support/OStream.h"
#include "mint/support/Path.h"

#if defined(_MSC_VER)
#pragma warning(disable:4355)
#endif 

namespace mint {

cl::Option<bool> optShowTargets("show-targets", cl::Group("debug"),
    cl::Description("Print out all targets."));

/** -------------------------------------------------------------------------
    Functor for comparing options.
 */
struct OptionNameComparator {
  inline bool operator()(
      const std::pair<String *, Node *> & lhs,
      const std::pair<String *, Node *> & rhs) {
    return lhs.first->value().compare(rhs.first->value()) < 0;
  }
};

Project::Project(BuildConfiguration * buildConfig, String * sourceRoot)
  : _buildConfig(buildConfig)
  , _sourceRoot(sourceRoot)
  , _buildRoot(StringRegistry::str(""))
  , _modules(_sourceRoot->value(), this)
  , _mainModule(NULL)
{
  if (buildConfig->prelude()) {
    _modules.setPrelude(buildConfig->prelude()->mainModule());
  }
}

void Project::setBuildRoot(StringRef buildRoot) {
  _buildRoot = String::create(buildRoot);
  // We want to ensure that this directory exists and is writeable
  if (!path::test(_buildRoot->value(), path::IS_DIRECTORY | path::IS_WRITABLE, false)) {
    exit(-1);
  }
}

Module * Project::mainModule() {
  if (_mainModule == NULL) {
    _mainModule = loadModule("");
    if (_mainModule == NULL) {
      diag::error() << "Main module for project at '" << sourceRoot() << "' not found";
      exit(-1);
    }
  }
  return _mainModule;
}

Module * Project::loadModule(StringRef name) {
  Module * module = _modules.load(name);
  if (module != NULL) {
    if (module->definition() != NULL) {
      Node * definition = module->definition();
      module->setDefinition(NULL);
      Evaluator e(module);
      if (!e.evalModuleContents(module, definition->asOper()) || diag::errorCount() > 0) {
        exit(-1);
      }
    }
  }
  return module;
}

void Project::createOptionDefaults() {
  OptionFinder finder(this);
  finder.visitModules();
  for (OptionFinder::const_iterator
      it = finder.begin(), itEnd = finder.end(); it != itEnd; ++it) {
    Object * option = *it;
    M_ASSERT(option->module() != NULL);
    Evaluator eval(option->module());
    if (!eval.ensureObjectContents(option)) {
      continue;
    }
    String * optName = String::cast(eval.attributeValue(option, "name"));
    if (_options.find(optName) == _options.end()) {
      Object * optSetting = new Object(option->location(), option, NULL);
      _options[optName] = optSetting;
    }
  }
}

void Project::getProjectOptions(SmallVectorImpl<StringDict<Object>::value_type> & options) const {
  // Sort options by name
  options.resize(_options.size());
  std::copy(_options.begin(), _options.end(), options.begin());
  std::sort(options.begin(), options.end(), OptionNameComparator());
}

bool Project::updateOptionValues(ArrayRef<Node *> nodes) {
  for (ArrayRef<Node *>::iterator ni = nodes.begin(), niEnd = nodes.end(); ni != niEnd; ++ni) {
    Node * n = *ni;
    switch (n->nodeKind()) {
      case Node::NK_SET_MEMBER: {
        Oper * op = static_cast<Oper *>(n);
        String * propName = String::cast(op->arg(0));
        Node * propValue = op->arg(1);
        if (propName->value() == "options") {
          Evaluator eval(_mainModule);
          propValue = eval.eval(propValue, NULL /*TypeRegistry::typeOptionList()*/);
          if (propValue != NULL) {
            Oper * optionList = propValue->requireOper();
            for (Oper::const_iterator li = optionList->begin(), liEnd = optionList->end(); li != liEnd; ++li) {
              Object * optionSetting = (*li)->requireObject();
              eval.ensureObjectContents(optionSetting);
              Object * optionProto = optionSetting->prototype();
              String * optionName = optionProto->name();
              M_ASSERT(optionName != NULL);
              _options[optionName] = optionSetting;
            }
          }
        }
        break;
      }

      default:
        diag::error(n->location()) << "Invalid node type for project configuration: "
            << n->nodeKind();
        return false;
    }
  }
  return true;
}

bool Project::setOption(StringRef optName, StringRef optValue) {
  // Convert dashes to underscores
  SmallString<32> optSym(optName);
  for (SmallString<32>::iterator si = optSym.begin(), siEnd = optSym.end(); si != siEnd; ++si) {
    if (*si == '-') {
      *si = '_';
    }
  }

  // Lookup in option table
  StringDict<Object>::const_iterator it = _options.find_as(optSym);
  if (it == _options.end()) {
    diag::error() << "Unknown option '" << optName << "'.";
    return false;
  }

  /// Lookup the value of a project option.
  Object * optSetting = it->second;
  M_ASSERT(optSetting != NULL);

  Object * optionDef = optSetting->prototype();
  AttributeLookup lookup;
  if (!optionDef->getAttribute("value", lookup)) {
    diag::error() << "Invalid definition for option '" << optName << "'.";
    exit(-1);
  }

  M_ASSERT(lookup.definition->type() != NULL);
  Node * newValue = NULL;
  if (lookup.definition->type()->isStringType()) {
    newValue = String::create(optValue);
  } else if (lookup.definition->type()->isBoolType()) {
    if (optValue == "true" || optValue == "yes" || optValue == "1") {
      newValue = Node::boolTrue();
    } else if (optValue == "false" || optValue == "no" || optValue == "0") {
      newValue = Node::boolFalse();
    }
  }

  if (newValue == NULL) {
    diag::error() << "Can't convert option value '" << optValue << "' to type "
        << lookup.definition->type();
    return false;
  }
  optSetting->attrs()[StringRegistry::str("value")] = newValue;
  return true;
}

bool Project::setOptions(CStringArray::const_iterator ai, CStringArray::const_iterator aiEnd) {
  while (ai < aiEnd) {
    StringRef optName = *ai++;
    // Look for an '=', otherwise use next entry in array as option value
    size_t eq = optName.find('=');
    if (eq != optName.npos) {
      // format a=b
      StringRef optValue = optName.substr(eq + 1);
      optName = optName.substr(0, eq);
      if (!setOption(optName, optValue)) {
        return false;
      }
    } else if (ai < aiEnd) {
      // format a b
      StringRef optValue = *ai++;
      if (!setOption(optName, optValue)) {
        return false;
      }
    } else {
      diag::error() << "Missing value for option '" << optName << "'.";
      return false;
    }
  }
  return true;
}

Node * Project::optionValue(StringRef name) {
  StringDict<Object>::const_iterator it = _options.find_as(name);
  if (it == _options.end()) {
    return NULL;
  }
  AttributeLookup lookup;
  if (it->second->getAttribute("value", lookup)) {
    return lookup.value;
  }
  return NULL;
}

bool Project::setConfig(ArrayRef<Node *> nodes) {
  Module * configurationModule = new Module(".configuration", this);
  configurationModule->setParentScope(&Fundamentals::get());
  configurationModule->addImportScope(_modules.prelude());
  Evaluator eval(configurationModule);

  for (ArrayRef<Node *>::iterator ni = nodes.begin(), niEnd = nodes.end(); ni != niEnd; ++ni) {
    Node * n = *ni;
    switch (n->nodeKind()) {
      case Node::NK_SET_MEMBER: {
        Oper * op = static_cast<Oper *>(n);
        String * propName = String::cast(op->arg(0));
        Node * propValue = op->arg(1);
        if (propName->value() == "cached_vars") {
          setConfigVars(propValue);
        }
        break;
      }

      default:
        diag::error(n->location()) << "Invalid node type for project configuration: "
            << n->nodeKind();
        return false;
    }
  }
  return true;
}

bool Project::setConfigVars(Node * n) {
  M_ASSERT(n->nodeKind() == Node::NK_MAKE_OBJECT);
  Oper * op = n->asOper();
  M_ASSERT(op != NULL);
  M_ASSERT(op->size() >= 1);
  Oper::const_iterator it = op->begin(), itEnd = op->end();
  it++;
  Evaluator eval(_mainModule);
  for (; it != itEnd; ++it) {
    if (!eval.setConfigVar(*it)) {
      return false;
    }
  }
  return true;
}

void Project::showOptions() const {
  // Search for options in project modules.
  SmallVector<StringDict<Object>::value_type, 32> options;
  getProjectOptions(options);

  console::out() << "Project options:\n";
  bool isTerminal = console::out().isTerminal();
  for (SmallVectorImpl<StringDict<Object>::value_type >::const_iterator
      it = options.begin(), itEnd = options.end();
      it != itEnd; ++it) {
    String * optName = it->first;
    Object * option = it->second;
    Evaluator eval(option->module());
    String * optHelp = String::dyn_cast(option->getAttributeValue("help"));
//    String * optGroup = String::dyn_cast(option->getAttributeValue("group"));
    //String * optAbbrev = String::dyn_cast(option->getPropertyValue("abbrev"));
    AttributeLookup value;
    if (!option->getAttribute("value", value)) {
      M_ASSERT(false) << "Option " << optName << " value not found!";
    }
    M_ASSERT(value.value != NULL);
    Type * optType = value.value->type();

    // Convert underscores to dashes.
    SmallString<32> name(optName->value());
    for (SmallVectorImpl<char>::iterator it = name.begin(), itEnd = name.end(); it != itEnd; ++it) {
      if (*it == '_') {
        *it = '-';
      }
    }

    // Print out the option
    if (isTerminal) {
      console::out().changeColor(OStream::GREEN, true);
    }
    console::out() << "  " << name;
    if (isTerminal) {
      console::out().resetColor();
    }
    if (optType != NULL) {
      console::out() << " : " << optType;
    }
    if (value.foundScope == option) {
      console::out() << " = " << value.value;
    } else if (!value.value->isUndefined()) {
      console::out() << " [default = " << value.value << "]";
    }
    console::out() << "\n";

    if (optHelp) {
      if (isTerminal) {
        console::out().changeColor(OStream::CYAN, false);
      }
      console::out().indent(6);
      console::out() << optHelp->value() << "\n";
      if (isTerminal) {
        console::out().resetColor();
      }
    }
  }
}

void Project::configure() {
  M_ASSERT(_mainModule != NULL) << "No main module defined for project " << _buildRoot;
  for (ModuleLoader::ModuleTable::const_iterator
      mi = _modules.modules().begin(), miEnd = _modules.modules().end(); mi != miEnd; ++mi) {
    Module * m = mi->second;
    Configurator config(this, m);
    config.visitModule(m);
  }
  if (diag::errorCount() > 0) {
    return;
  }
}

void Project::generate() {
  M_ASSERT(_mainModule != NULL) << "No main module defined for project " << _buildRoot;
  Configurator config(this, _mainModule);
  config.performActions(_mainModule);
  if (diag::errorCount() > 0) {
    return;
  }
}

void Project::gatherTargets() {
  M_ASSERT(_mainModule != NULL) << "No main module defined for project " << _buildRoot;
  TargetMgr * targetMgr = _buildConfig->targetMgr();
  targetMgr->addRootDirectory(_sourceRoot->value());
  TargetFinder finder(targetMgr, this);
  for (ModuleLoader::ModuleTable::const_iterator
      mi = _modules.modules().begin(), miEnd = _modules.modules().end(); mi != miEnd; ++mi) {
    finder.visitModule(mi->second);
  }
  if (diag::errorCount() > 0) {
    return;
  }
  GC::sweep();
  if (optShowTargets) {
    GraphWriter writer(console::out());
    writer.write(_mainModule);
  }
}

void Project::writeOptions(GraphWriter & writer) const {
  // TODO: Need to escape this string.
  writer.strm() << "project '" << sourceRoot() << "' {\n";
  writer.indent();
  writer.strm() << "  options = ";

  SmallVector<StringDict<Object>::value_type, 32> options;
  getProjectOptions(options);
  SmallVector<Node *, 32> optionObjects;
  for (SmallVectorImpl<StringDict<Object>::value_type >::const_iterator
      it = options.begin(), itEnd = options.end(); it != itEnd; ++it) {
    optionObjects.push_back(it->second);
  }

  Oper * optionList = Oper::createList(Location(), NULL, optionObjects);
  writer.write(optionList, false);
  writer.unindent();
  writer.strm() << "}\n";
}

void Project::writeConfig(GraphWriter & writer) const {
  diag::status() << "Writing project configuration\n";
  // TODO: Need to escape this string.
  writer.strm() << "project '" << sourceRoot() << "' {\n";
  writer.writeCachedVars(_mainModule);
  writer.strm() << "}\n";
}

void Project::writeMakefiles() const {
  SmallString<128> makefilePath(_mainModule->buildDir());
  path::combine(makefilePath, "Makefile");
  MakefileGenerator gen(makefilePath, _mainModule, _buildConfig->targetMgr());
  gen.writeModule();
  for (ModuleLoader::ModuleTable::const_iterator
      mi = _modules.modules().begin(), miEnd = _modules.modules().end(); mi != miEnd; ++mi) {
    Module * m = mi->second;
    if (m != _mainModule) {
      SmallString<128> makefilePath(m->buildDir());
      path::combine(makefilePath, "Makefile");
      MakefileGenerator mgen(makefilePath, m, _buildConfig->targetMgr());
      mgen.writeModule();
    }
  }
}

Object * Project::lookupObject(StringRef name) {
  // TODO: Handle dots and colons
  Node * n = mainModule()->getAttributeValue(name);
  if (n != NULL) {
    return n->asObject();
  }
  return NULL;
}

/// Given a source path, create a target for that path, composing it from the prototype
/// and the parameter objects. If a target already exists for that path, then return it.
Object * Project::targetForSource(String * source, Object * proto, Oper * params) {
  return NULL;
}

/// Given an output path, create a target for that path, composing it from the prototype
/// and the parameter objects. If a target already exists for that path, then return it.
Object * Project::targetForOutput(String * output, Object * proto, Oper * params) {
  return NULL;
}

void Project::trace() const {
  safeMark(_sourceRoot);
  safeMark(_buildRoot);
  _modules.trace();
  _options.trace();
  _targetsForSource.trace();
  _targetsForOutput.trace();
  safeMark(_mainModule);
}

}
