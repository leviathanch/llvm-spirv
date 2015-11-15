//===- SPIRVUtil.h - SPIR-V Utility Functions --------------------*- C++ -*-===//
//
//                     The LLVM/SPIRV Translator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Copyright (c) 2014 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal with the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimers.
// Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimers in the documentation
// and/or other materials provided with the distribution.
// Neither the names of Advanced Micro Devices, Inc., nor the names of its
// contributors may be used to endorse or promote products derived from this
// Software without specific prior written permission.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH
// THE SOFTWARE.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines SPIR-V utility functions.
///
//===----------------------------------------------------------------------===//

#ifndef SPIRVUTIL_H_
#define SPIRVUTIL_H_

#include <algorithm>
#include <cassert>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace SPIRV{

#define SPIRV_DEF_NAMEMAP(Type,MapType) \
typedef SPIRVMap<Type, std::string> MapType; \
inline MapType getNameMap(Type){ MapType MT; return MT;}

// A bi-way map
template<class Ty1, class Ty2, class Identifier = void>
struct SPIRVMap {
public:
  typedef Ty1 KeyTy;
  typedef Ty2 ValueTy;
  // Initialize map entries
  void init();

  static Ty2 map(Ty1 Key) {
    Ty2 Val;
    bool Found = find(Key, &Val);
    assert (Found && "Invalid key");
    return Val;
  }

  static Ty1 rmap(Ty2 Key) {
    Ty1 Val;
    bool Found = rfind(Key, &Val);
    assert (Found && "Invalid key");
    return Val;
  }

  static const SPIRVMap& getMap() {
    static const SPIRVMap Map(false);
    return Map;
  }

  static const SPIRVMap& getRMap() {
    static const SPIRVMap Map(true);
    return Map;
  }

  static void foreach(std::function<void(Ty1, Ty2)>F) {
    for (auto &I:getMap().Map)
      F(I.first, I.second);
  }

  // For each key/value in the map executes function \p F.
  // If \p F returns false break the iteration.
  static void foreach_conditional(std::function<bool(const Ty1&, Ty2)>F) {
    for (auto &I:getMap().Map) {
      if (!F(I.first, I.second))
        break;
    }
  }

  static bool find(Ty1 Key, Ty2 *Val = nullptr) {
    const SPIRVMap& Map = getMap();
    typename MapTy::const_iterator Loc = Map.Map.find(Key);
    if(Loc == Map.Map.end())
      return false;
    if (Val)
      *Val = Loc->second;
    return true;
  }

  static bool rfind(Ty2 Key, Ty1 *Val = nullptr) {
    const SPIRVMap& Map = getRMap();
    typename RevMapTy::const_iterator Loc = Map.RevMap.find(Key);
    if (Loc == Map.RevMap.end())
      return false;
    if (Val)
      *Val = Loc->second;
    return true;
  }
  SPIRVMap():IsReverse(false){}
protected:
  SPIRVMap(bool Reverse):IsReverse(Reverse){
    init();
  }
  typedef std::map<Ty1, Ty2> MapTy;
  typedef std::map<Ty2, Ty1> RevMapTy;

  void add(Ty1 V1, Ty2 V2) {
    if (IsReverse) {
      RevMap[V2] = V1;
      return;
    }
    Map[V1] = V2;
  }
  MapTy Map;
  RevMapTy RevMap;
  bool IsReverse;
};

inline std::vector<std::string>
getVec(const std::string &S, char Delim) {
  std::vector<std::string> Strs;
  std::stringstream SS(S);
  std::string Item;
  while (std::getline(SS, Item, Delim))
    Strs.push_back(Item);
  return Strs;
}

inline std::unordered_set<std::string>
getUnordSet(const std::string &S, char Delim = ' ') {
  std::unordered_set<std::string> Strs;
  std::stringstream SS(S);
  std::string Item;
  while (std::getline(SS, Item, Delim))
    Strs.insert(Item);
  return Strs;
}

inline std::set<std::string>
getSet(const std::string &S, char Delim = ' ') {
  std::set<std::string> Strs;
  std::stringstream SS(S);
  std::string Item;
  while (std::getline(SS, Item, Delim))
    Strs.insert(Item);
  return Strs;
}

template<typename VT, typename KT>
VT map(KT Key) {
  return SPIRVMap<KT, VT>::map(Key);
}

template<typename KT, typename VT>
KT rmap(VT V) {
  return SPIRVMap<KT, VT>::rmap(V);
}

template<typename VT, typename KT>
std::unordered_set<VT>
map(const std::unordered_set<KT> &KSet) {
  VT V;
  std::unordered_set<VT> VSet;
  for (auto &I:KSet)
    if (SPIRVMap<KT, VT>::find(I, &V))
      VSet.insert(V);
  return VSet;
}

template<typename VT, typename KT>
std::set<VT>
map(const std::set<KT> &KSet) {
  VT V;
  std::set<VT> VSet;
  for (auto &I:KSet)
    if (SPIRVMap<KT, VT>::find(I, &V))
      VSet.insert(V);
  return VSet;
}

template<typename KT, typename VT>
std::unordered_set<KT>
rmap(const std::unordered_set<VT> &KSet) {
  KT V;
  std::unordered_set<KT> VSet;
  for (auto &I:KSet)
    if (SPIRVMap<KT, VT>::rfind(I, &V))
      VSet.insert(V);
  return VSet;
}

template<typename KT, typename VT>
std::set<KT>
rmap(const std::set<VT> &KSet) {
  KT V;
  std::set<KT> VSet;
  for (auto &I:KSet)
    if (SPIRVMap<KT, VT>::rfind(I, &V))
      VSet.insert(V);
  return VSet;
}

template<typename K>
std::string
getName(K Key) {
  std::string Name;
  if (SPIRVMap<K, std::string>::find(Key, &Name))
    return Name;
  return "";
}

template<typename K>
bool getByName(const std::string &Name, K &Key) {
  return SPIRVMap<K, std::string>::rfind(Name, &Key);
}

// Add a number as a string to a string
template<class T>
std::string
concat(const std::string& s, const T& n) {
  std::stringstream ss;
  ss << s << n;
  return ss.str();
}

inline std::string
concat(const std::string &S1, const std::string &S2, char Delim = ' ') {
  std::string S;
  if (S1.empty())
    S = S2;
  else if (!S2.empty())
    S = S1 + Delim + S2;
  return S;
}

inline std::string
operator+(const std::string& s, int n) {
  return concat(s, n);
}

inline std::string
operator+(const std::string& s, unsigned n) {
  return concat(s, n);
}

template<typename T>
std::string
getStr(const T &C, char Delim = ' ') {
  std::stringstream SS;
  bool First = true;
  for (auto &I:C) {
    if (!First)
      SS << Delim;
    else
      First = false;
    SS << I;
  }
  return SS.str();
}

template<class MapTy>
unsigned mapBitMask(unsigned BM) {
  unsigned Res = 0;
  MapTy::foreach([&](typename MapTy::KeyTy K, typename MapTy::ValueTy V){
    Res |= BM & (unsigned)K ? (unsigned)V : 0;
  });
  return Res;
}

template<class MapTy>
unsigned rmapBitMask(unsigned BM) {
  unsigned Res = 0;
  MapTy::foreach([&](typename MapTy::KeyTy K, typename MapTy::ValueTy V){
    Res |= BM & (unsigned)V ? (unsigned)K : 0;
  });
  return Res;
}

// Get the number of words used for encoding a string literal in SPIRV
inline unsigned
getSizeInWords(const std::string& Str) {
  assert(Str.length()/4 + 1 <= std::numeric_limits<unsigned>::max());
  return static_cast<unsigned>(Str.length()/4 + 1);
}

template<typename T>
inline std::vector<T>
getVec(T Op1) {
  std::vector<T> V;
  V.push_back(Op1);
  return V;
}

template<typename T>
inline std::vector<T>
getVec(T Op1, T Op2) {
  std::vector<T> V;
  V.push_back(Op1);
  V.push_back(Op2);
  return V;
}

template<typename T>
inline std::vector<T>
getVec(T Op1, T Op2, T Op3) {
  std::vector<T> V;
  V.push_back(Op1);
  V.push_back(Op2);
  V.push_back(Op3);
  return V;
}

template<typename T>
inline std::vector<T>
getVec(T Op1, const std::vector<T> &Ops2) {
  std::vector<T> V;
  V.push_back(Op1);
  V.insert(V.end(), Ops2.begin(), Ops2.end());
  return V;
}

template<typename MapTy, typename FuncTy>
typename MapTy::mapped_type
getOrInsert(
    MapTy &Map,
    typename MapTy::key_type Key,
    FuncTy Func){
  typename MapTy::iterator Loc = Map.find(Key);
  if (Loc != Map.end())
    return Loc->second;
  typename MapTy::mapped_type  NF = Func();
  Map[Key] = NF;
  return NF;
}

}


#endif /* SPIRVUTIL_HPP_ */