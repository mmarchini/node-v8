// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>

#include "src/torque/declarable.h"
#include "src/torque/type-oracle.h"
#include "src/torque/types.h"

namespace v8 {
namespace internal {
namespace torque {

std::string Type::ToString() const {
  if (aliases_.size() == 0) return ToExplicitString();
  if (aliases_.size() == 1) return *aliases_.begin();
  std::stringstream result;
  int i = 0;
  for (const std::string& alias : aliases_) {
    if (i == 0) {
      result << alias << " (aka. ";
    } else if (i == 1) {
      result << alias;
    } else {
      result << ", " << alias;
    }
    ++i;
  }
  result << ")";
  return result.str();
}

bool Type::IsSubtypeOf(const Type* supertype) const {
  if (const UnionType* union_type = UnionType::DynamicCast(supertype)) {
    return union_type->IsSupertypeOf(this);
  }
  const Type* subtype = this;
  while (subtype != nullptr) {
    if (subtype == supertype) return true;
    subtype = subtype->parent();
  }
  return false;
}

// static
const Type* Type::CommonSupertype(const Type* a, const Type* b) {
  int diff = a->Depth() - b->Depth();
  const Type* a_supertype = a;
  const Type* b_supertype = b;
  for (; diff > 0; --diff) a_supertype = a_supertype->parent();
  for (; diff < 0; ++diff) b_supertype = b_supertype->parent();
  while (a_supertype && b_supertype) {
    if (a_supertype == b_supertype) return a_supertype;
    a_supertype = a_supertype->parent();
    b_supertype = b_supertype->parent();
  }
  ReportError("types " + a->ToString() + " and " + b->ToString() +
              " have no common supertype");
}

int Type::Depth() const {
  int result = 0;
  for (const Type* current = parent_; current; current = current->parent_) {
    ++result;
  }
  return result;
}

bool Type::IsAbstractName(const std::string& name) const {
  if (!IsAbstractType()) return false;
  return AbstractType::cast(this)->name() == name;
}

std::string AbstractType::GetGeneratedTNodeTypeName() const {
  std::string result = GetGeneratedTypeName();
  DCHECK_EQ(result.substr(0, 6), "TNode<");
  result = result.substr(6, result.length() - 7);
  return result;
}

std::string FunctionPointerType::ToExplicitString() const {
  std::stringstream result;
  result << "builtin (";
  bool first = true;
  for (const Type* t : parameter_types_) {
    if (!first) {
      result << ", ";
      first = false;
    }
    result << t;
  }
  result << ") => " << return_type_;
  return result.str();
}

std::string FunctionPointerType::MangledName() const {
  std::stringstream result;
  result << "FT";
  for (const Type* t : parameter_types_) {
    std::string arg_type_string = t->MangledName();
    result << arg_type_string.size() << arg_type_string;
  }
  std::string return_type_string = return_type_->MangledName();
  result << return_type_string.size() << return_type_string;
  return result.str();
}

std::string UnionType::ToExplicitString() const {
  std::stringstream result;
  result << "(";
  bool first = true;
  for (const Type* t : types_) {
    if (!first) {
      result << " | ";
    }
    first = false;
    result << t;
  }
  result << ")";
  return result.str();
}

std::string UnionType::MangledName() const {
  std::stringstream result;
  result << "UT";
  for (const Type* t : types_) {
    std::string arg_type_string = t->MangledName();
    result << arg_type_string.size() << arg_type_string;
  }
  return result.str();
}

std::string UnionType::GetGeneratedTNodeTypeName() const {
  if (types_.size() <= 3) {
    std::set<std::string> members;
    for (const Type* t : types_) {
      members.insert(t->GetGeneratedTNodeTypeName());
    }
    if (members == std::set<std::string>{"Smi", "HeapNumber"}) {
      return "Number";
    }
    if (members == std::set<std::string>{"Smi", "HeapNumber", "BigInt"}) {
      return "Numeric";
    }
  }
  return parent()->GetGeneratedTNodeTypeName();
}

std::ostream& operator<<(std::ostream& os, const Signature& sig) {
  os << "(";
  for (size_t i = 0; i < sig.parameter_types.types.size(); ++i) {
    if (i > 0) os << ", ";
    if (!sig.parameter_names.empty()) os << sig.parameter_names[i] << ": ";
    os << sig.parameter_types.types[i];
  }
  if (sig.parameter_types.var_args) {
    if (sig.parameter_names.size()) os << ", ";
    os << "...";
  }
  os << ")";
  os << ": " << sig.return_type;

  if (sig.labels.empty()) return os;

  os << " labels ";
  for (size_t i = 0; i < sig.labels.size(); ++i) {
    if (i > 0) os << ", ";
    os << sig.labels[i].name;

    if (sig.labels[i].types.size() > 0) os << "(" << sig.labels[i].types << ")";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const TypeVector& types) {
  for (size_t i = 0; i < types.size(); ++i) {
    if (i > 0) os << ", ";
    os << types[i];
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const ParameterTypes& p) {
  for (size_t i = 0; i < p.types.size(); ++i) {
    if (i > 0) os << ", ";
    os << p.types[i];
  }
  if (p.var_args) {
    if (p.types.size() > 0) os << ", ";
    os << "...";
  }
  return os;
}

bool Signature::HasSameTypesAs(const Signature& other) const {
  if (!(parameter_types.types == other.parameter_types.types &&
        parameter_types.var_args == other.parameter_types.var_args &&
        return_type == other.return_type)) {
    return false;
  }
  if (labels.size() != other.labels.size()) {
    return false;
  }
  size_t i = 0;
  for (auto l : labels) {
    if (l.types != other.labels[i++].types) {
      return false;
    }
  }
  return true;
}

bool IsAssignableFrom(const Type* to, const Type* from) {
  if (to == from) return true;
  if (from->IsSubtypeOf(to) && !from->IsConstexpr()) return true;
  return TypeOracle::IsImplicitlyConverableFrom(to, from);
}

bool IsCompatibleSignature(const Signature& sig, const TypeVector& types,
                           const std::vector<Label*>& labels) {
  auto i = sig.parameter_types.types.begin();
  if (sig.parameter_types.types.size() > types.size()) return false;
  // TODO(danno): The test below is actually insufficient. The labels'
  // parameters must be checked too. ideally, the named part of
  // LabelDeclarationVector would be factored out so that the label count and
  // parameter types could be passed separately.
  if (sig.labels.size() != labels.size()) return false;
  for (auto current : types) {
    if (i == sig.parameter_types.types.end()) {
      if (!sig.parameter_types.var_args) return false;
      if (!IsAssignableFrom(TypeOracle::GetObjectType(), current)) return false;
    } else {
      if (!IsAssignableFrom(*i++, current)) return false;
    }
  }
  return true;
}

bool operator<(const Type& a, const Type& b) {
  return a.MangledName() < b.MangledName();
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
