/* ================================================================== *
 * Project
 * ================================================================== */

#include "mint/build/TargetMgr.h"
#include "mint/build/TargetFinder.h"

#include "mint/eval/Evaluator.h"

#include "mint/graph/Object.h"
#include "mint/graph/GraphWriter.h"

#include "mint/project/BuildConfiguration.h"
#include "mint/project/Configurator.h"
#include "mint/project/Project.h"

#include "mint/support/Assert.h"
#include "mint/support/Diagnostics.h"
#include "mint/support/OStream.h"
#include "mint/support/Path.h"

namespace mint {

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

/** -------------------------------------------------------------------------
    Functor for comparing strings.
 */
struct StringComparator {
  inline bool operator()(const String * lhs, const String * rhs) {
    return lhs->value().compare(rhs->value()) < 0;
  }
};

Project::Project(BuildConfiguration * buildConfig, StringRef sourceRoot)
  : _buildConfig(buildConfig)
  , _buildRoot(NULL)
  , _modules(sourceRoot, this)
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
    _mainModule = _modules.load("");
    if (_mainModule == NULL) {
      diag::error() << "Main module for project at '" << sourceRoot() << "' not found";
      exit(-1);
    }
  }
  return _mainModule;
}

Module * Project::loadModule(StringRef name) {
  return _modules.load(name);
}

void Project::makeProjectOptions() {
  if (_options.empty()) {
    SmallVector<Node *, 32> options;
    _modules.findOptions(options);
    for (SmallVectorImpl<Node *>::const_iterator it = options.begin(), itEnd = options.end();
        it != itEnd; ++it) {
      Object * option = (*it)->asObject();
      M_ASSERT(option->module() != NULL);
      Evaluator eval(option->module());
      if (!eval.ensureObjectContents(option)) {
        continue;
      }
      String * optName = String::cast(eval.attributeValue(option, "name"));
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
  GC::sweep();
  Configurator config(this, _mainModule);
  config.visitModule(_mainModule);
  if (diag::errorCount() > 0) {
    return;
  }
  GC::sweep();
  config.performActions(_mainModule);
  if (diag::errorCount() > 0) {
    return;
  }
  GC::sweep();
//  GraphWriter writer(console::out());
//  writer.write(_mainModule);
//  if (diag::errorCount() > 0) {
//    return;
//  }

  TargetMgr * targetMgr = new TargetMgr();
  TargetFinder finder(targetMgr, this);
  finder.visitModule(_mainModule);
  //GC::sweep();
  if (diag::errorCount() > 0) {
    return;
  }

  console::out() << "Available targets:\n";
  for (TargetMap::const_iterator it = targetMgr->targets().begin(), itEnd = targetMgr->targets().end(); it != itEnd; ++it) {
    String * path = it->second->path();
    if (path != NULL) {
      console::out() << "  " << path->value() << "\n";
      it->second->checkState();
    }
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

  Oper * optionList = Oper::create(Node::NK_LIST, Location(), NULL, optionObjects);
  writer.write(optionList, false);
  writer.unindent();

  writer.strm() << "}\n";
}

void Project::writeProjectConfig(GraphWriter & writer) const {
}

void Project::trace() const {
  safeMark(_buildRoot);
  _modules.trace();
  _options.trace();
  safeMark(_mainModule);
}

}
