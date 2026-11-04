// SPDX-License-Identifier: GPL-3.0-or-later
// Compile local source through the actual SDK transformer; never execute it.
#include "runtime/mod_loader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
int main(int argc,char**argv) {
  if(argc!=4)return 2;
  namespace fs=std::filesystem;
  const fs::path package=argv[1],base=argv[2],output=argv[3];
  if(fs::exists(output))return 3;
  auto discovered=cc2::host::DiscoverActiveScreenExtensions({package.wstring()});
  if(!discovered.diagnostics.empty()||discovered.extensions.size()!=3) {
    for(const auto& d:discovered.diagnostics)std::wcerr<<d<<L'\n';return 4;
  }
  fs::create_directories(output);
  for(const auto& extension:discovered.extensions) {
    std::ifstream file(base/extension.screen,std::ios::binary);
    if(!file)return 5;
    std::string source((std::istreambuf_iterator<char>(file)),{}),modified;
    if(cc2::host::TransformScreenSource(extension.profiles,source,&modified)!=cc2::host::ScreenSourceEditResult::Applied) {
      std::cerr<<"Rejected "<<extension.screen<<" hash="<<cc2::host::ScreenSourceSha256(source)<<'\n';return 6;
    }
    modified+='\n';modified+=extension.source;
    lua_State* state=luaL_newstate();if(!state)return 7;
    const bool valid=luaL_loadbufferx(state,modified.data(),modified.size(),extension.screen.c_str(),"t")==LUA_OK;
    if(!valid)std::cerr<<lua_tostring(state,-1)<<'\n';
    lua_close(state);if(!valid)return 8;
    auto target=output/fs::path(extension.screen).filename();
    std::ofstream(target,std::ios::binary).write(modified.data(),modified.size());
    std::cout<<"PASS syntax-only SDK delivery "<<extension.screen<<'\n';
  }
}
