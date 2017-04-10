#ifndef FILEIO_CPP_
#define FILEIO_CPP_

#include "json.hpp"
#include <iostream>
#include <fstream>
#include "context.hpp"
#include "instantiable.hpp"
#include "namespace.hpp"
#include <unordered_map>

namespace CoreIR {

typedef unordered_map<string,json> jsonmap;

using json = nlohmann::json;

Type* json2Type(Context* c, json jt);
Args json2Args(Context* c, Params p, json j);
Params json2Params(Context* c, json j);

Instantiable* getSymbol(Context* c, string nsname, string iname);

Module* loadModule(Context* c, string filename, bool* err) {
  std::fstream file;
  file.open(filename);
  if (!file.is_open()) {
    *err =true;
    Error e;
    e.message("Cannot open file " + filename);
    e.fatal();
    c->error(e);
    return nullptr;
  }
  json j;
  file >> j;
  Module* mod = nullptr;

  try {
    string topnsname = j.at("top").at(0);
    string topmodname = j.at("top").at(1);
    
    //First load all the module declarations
    vector<std::pair<Module*,json>> modqueue;
    //Get or create namespace
    for (auto jnsmap : j.at("namespaces").get<jsonmap>() ) {
      string nsname = jnsmap.first;
      json jns = jnsmap.second;
      Namespace* ns;
      if (c->hasNamespace(nsname) ) ns = c->getNamespace(nsname);
      else ns = c->newNamespace(nsname);
      //Load Modules
      if (jns.count("modules")) {
        for (auto jmodmap : jns.at("modules").get<jsonmap>()) {
          //Figure out type;
          string jmodname = jmodmap.first;
          json jmod = jmodmap.second;
          Type* t = json2Type(c,jmod.at("type"));
          
          Params configparams;
          if (jmod.count("configparams")) {
            configparams = json2Params(c,jmod.at("configparams"));
          }
          Module* m = ns->newModuleDecl(jmodname,t,configparams);
          modqueue.push_back({m,jmod});
        }
      }
      if (jns.count("generators")) {
        //TODO Load Generators
        


      }
    }

    //Now do all the ModuleDefinitions
    for (auto mq : modqueue) {
      Module* m = mq.first;
      json jmod = mq.second;
      // TODO Module metadata
      if (!jmod.count("def")) continue;
      
      json jdef = jmod.at("def");
      ModuleDef* mdef = m->newModuleDef();
      // TODO ModuleDef metadata
      // TODO moduledef implementations
      if (jdef.count("instances")) {
        for (auto jinstmap : jdef.at("instances").get<jsonmap>()) {
          string instname = jinstmap.first;
          json jinst = jinstmap.second;
          json jinstRef = jinst.at("instref");
          
          // This function can throw an error
          Instantiable* instRef = getSymbol(c,jinstRef.at(0),jinstRef.at(1));
          
          Args config;
          if (jinst.count("config")) {
            config = json2Args(c,instRef->getConfigParams(),jinst.at("config"));
          }

          //Assume that if there are genargs, it is a generator
          if (jinst.count("genargs")) { // This is a generator
            cout << "NYI Generator instances: " << instname << endl;
            assert(instRef->isKind(GEN));
            assert(false);
          }
          else { // This is a module
            assert(instRef->isKind(MOD));
            mdef->addInstance(instname,(Module*) instRef,config);
          }
        } // End Instances
      }

      //Connections
      if (jdef.count("connections")) {
        for (auto jcon : jdef.at("connections").get<vector<vector<json>>>()) {
          //TODO connection metadata
          vector<string> wA = jcon[0].get<vector<string>>();
          vector<string> wB = jcon[1].get<vector<string>>();
          string instA = wA[0];
          string instB = wB[0];
          wA.erase(wA.begin());
          wB.erase(wB.begin());
          WirePath pathA = {instA,wA};
          WirePath pathB = {instB,wB};
          mdef->wire(pathA,pathB);
        }
      }
      
      //Add Def back in
      m->addDef(mdef);
    } //End Module loop
    
    //Reference Top
    Instantiable* topInst = getSymbol(c,topnsname,topmodname);
    assert(topInst->isKind(MOD));
    mod = (Module*) topInst;
  
  } catch(std::exception& exc) {
    *err = true;
    Error e; 
    e.message(exc.what());
    c->error(e);
  }

  return mod;
}

Instantiable* getSymbol(Context* c, string nsname, string iname) {
  if (c->hasNamespace(nsname)) {
    if (c->getNamespace(nsname)->hasInstantiable(iname)) {
      return c->getNamespace(nsname)->getInstantiable(iname);
    }
  }
  string msg = "Missing Symbol: " + nsname + "." + iname;
  throw std::runtime_error(msg);
  return nullptr;
}

Params json2Params(Context* c, json j) {
  Params g;
  if (j.is_null()) return g;
  for (auto jmap : j.get<jsonmap>()) {
    g[jmap.first] = Str2Param(jmap.second.get<string>());
  }
  return g;
}

Args json2Args(Context* c, Params genparams, json j) {
  Args gargs; 

  //TODO this following code should make sure there are the same number of key-value pairs
  for (auto pmap : genparams) {
    string key = pmap.first;
    Param kind = pmap.second;
    Arg* g;
    switch(kind) {
      case AINT : g = c->int2Arg(j.at(key).get<int>()); break;
      case ASTRING : g = c->str2Arg(j.at(key).get<string>()); break;
      case ATYPE : g = c->type2Arg(json2Type(c,j.at(key))); break;
      default :  throw std::runtime_error(Param2Str(kind) + "is not a valid arg param!");
    }
    gargs[key] = g;
  }
  return gargs;
}

Type* json2Type(Context* c, json jt) {
  if (jt.type() == json::value_t::string) {
    //Will be bitIn or BitOut
    string kind = jt.get<string>();
    if (kind == "BitIn") return c->BitIn();
    else if (kind == "BitOut") return c->BitOut();
    else if (kind == "Any") return c->Any();
    else throw std::runtime_error(kind + " is not a type!");
  }
  else if (jt.type() == json::value_t::array) {
    vector<json> args = jt.get<vector<json>>();
    string kind = args[0].get<string>();
    if (kind == "Array") {
      uint n = args[1].get<uint>();
      Type* t = json2Type(c,args[2]);
      return c->Array(n,t);
    }
    else if (kind == "Record") {
      vector<myPair<string,Type*>> rargs;
      for (auto it : args[1].get<vector<vector<json>>>())
        
        rargs.push_back({it[0].get<string>(),json2Type(c,it[1])});
      return c->Record(rargs);
    }
    else {
      cout << "ERROR NYI!: " << args[0].get<string>() << endl;
      assert(false);
    }
  }
  else throw std::runtime_error("Error parsing Type");
  return c->Any();
}

//true cannot open file
void saveModule(Module* m, string filename, bool* err) {
  Context* c = m->getContext();
  std::ofstream file(filename);
  if (!file.is_open()) {
    *err =true;
    Error e;
    e.message("Cannot open file " + filename);
    e.fatal();
    c->error(e);
    return;
  }

  //TODO I should gather only the dependent modules
  json j;
  j["top"] = json::array({m->getNamespace()->getName(),m->getName()});
  for (auto nsmap: c->getNamespaces()) {
    j["namespaces"][nsmap.first] = nsmap.second->toJson();
  }
  file << std::setw(2) << j;
  return;
}



json Args2Json(Args args);
json Params2Json(Params gp);
json Wireable2Json(Wireable* w);

json Type::toJson() { 
  return TypeKind2Str(kind);
}
json ArrayType::toJson() {
  return json::array({TypeKind2Str(kind),len,elemType->toJson()});
}
json RecordType::toJson() {
  json jfields;
  for (auto sel : _order) jfields.push_back(json::array({sel,record[sel]->toJson()}));
  return json::array({TypeKind2Str(kind),jfields});
}

json Namespace::toJson() {
  json j;
  if (!moduleList.empty()) {
    json jmods;
    for (auto m : moduleList) jmods[m.first] = m.second->toJson();
    j["modules"] = jmods;
  }
  if (!generatorList.empty()) {
    json jgens;
    for (auto g : generatorList) jgens[g.first] = g.second->toJson();
    j["generators"] = jgens;
  }
  return j;
}

json Instantiable::toJson() {
  json j;
  if (!configparams.empty()) {
    j["configparams"] = Params2Json(configparams);
  }
  if (!metadata.empty()) {
    j["metadata"] = metadata.toJson();
  }
  return j;
}

json Module::toJson() {
  json j = Instantiable::toJson();
  j["type"] = type->toJson();
  if (this->hasDef()) {
    j["def"] = this->getDef()->toJson();
  }
  return j;
}

json Generator::toJson() {
  json j = Instantiable::toJson();
  j["genparams"] = Params2Json(genparams);
  j["typegen"] = "TODO";
  return j;
}

json ModuleDef::toJson() {
  json j;
  if (!metadata.empty()) {
    j["metadata"] = metadata.toJson();
  }
  if (!implementations.empty()) {
    j["implementations"] = implementations.toJson();
  }
  if (!instances.empty()) {
    json jinsts;
    for ( auto instmap : instances) {
      jinsts[instmap.first] = instmap.second->toJson();
    }
    j["instances"] = jinsts;
  }
  if (!connections.empty()) {
    json jcons;
    for (auto connection : connections) {
      jcons.push_back(connection.toJson());
    }
    j["connections"] = jcons;
  }
  return j;
}

json Connection::toJson() {
  json j = json::array({Wireable2Json(first), Wireable2Json(second)});
  if (!metadata.empty()) {
    j.push_back(metadata.toJson());
  }
  return j;
}

json Wireable2Json(Wireable* w) {
  WirePath path = w->getPath();
  json j = json::array({path.first});
  for (auto str : path.second) j.push_back(str);
  return j;
}

json Instance::toJson() {
  json j;
  j["instref"] = json::array({instRef->getNamespace()->getName(),instRef->getName()});
  if (this->isGen()) {
    assert(!genargs.empty());
    j["genargs"] = Args2Json(genargs);
  }
  if (this->hasConfig()) {
    j["config"] = Args2Json(this->getConfig());
  }
  if (!metadata.empty()) {
    j["metadata"] = metadata.toJson();
  }
  return j;
}

json Metadata::toJson() {
  json j;
  for (auto it : metadata) j[it.first] = it.second;
  return j;
}

json Params2Json(Params gp) {
  json j = {};
  for (auto it : gp) j[it.first] = Param2Str(it.second);
  return j;
}

json Args2Json(Args args) {
  json j;
  for (auto it : args) j[it.first] = it.second->toJson();
  return j;
}

json ArgString::toJson() { return str; }
json ArgInt::toJson() { return i; }
json ArgType::toJson() { return t->toJson(); }

}//CoreIR namespace

#endif //FILEIO_CPP_
