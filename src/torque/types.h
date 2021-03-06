// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPES_H_
#define V8_TORQUE_TYPES_H_

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "src/base/optional.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

static const char* const CONSTEXPR_TYPE_PREFIX = "constexpr ";
static const char* const NEVER_TYPE_STRING = "never";
static const char* const CONSTEXPR_BOOL_TYPE_STRING = "constexpr bool";
static const char* const BOOL_TYPE_STRING = "bool";
static const char* const VOID_TYPE_STRING = "void";
static const char* const ARGUMENTS_TYPE_STRING = "constexpr Arguments";
static const char* const CONTEXT_TYPE_STRING = "Context";
static const char* const OBJECT_TYPE_STRING = "Object";
static const char* const STRING_TYPE_STRING = "String";
static const char* const CODE_TYPE_STRING = "Code";
static const char* const INTPTR_TYPE_STRING = "intptr";
static const char* const CONST_INT31_TYPE_STRING = "constexpr int31";
static const char* const CONST_INT32_TYPE_STRING = "constexpr int32";
static const char* const CONST_FLOAT64_TYPE_STRING = "constexpr float64";

class Label;

class TypeBase {
 public:
  enum class Kind { kAbstractType, kFunctionPointerType, kUnionType };
  virtual ~TypeBase() {}
  bool IsAbstractType() const { return kind() == Kind::kAbstractType; }
  bool IsFunctionPointerType() const {
    return kind() == Kind::kFunctionPointerType;
  }
  bool IsUnionType() const { return kind() == Kind::kUnionType; }

 protected:
  explicit TypeBase(Kind kind) : kind_(kind) {}
  Kind kind() const { return kind_; }

 private:
  const Kind kind_;
};

#define DECLARE_TYPE_BOILERPLATE(x)                         \
  static x* cast(TypeBase* declarable) {                    \
    DCHECK(declarable->Is##x());                            \
    return static_cast<x*>(declarable);                     \
  }                                                         \
  static const x* cast(const TypeBase* declarable) {        \
    DCHECK(declarable->Is##x());                            \
    return static_cast<const x*>(declarable);               \
  }                                                         \
  static x* DynamicCast(TypeBase* declarable) {             \
    if (!declarable) return nullptr;                        \
    if (!declarable->Is##x()) return nullptr;               \
    return static_cast<x*>(declarable);                     \
  }                                                         \
  static const x* DynamicCast(const TypeBase* declarable) { \
    if (!declarable) return nullptr;                        \
    if (!declarable->Is##x()) return nullptr;               \
    return static_cast<const x*>(declarable);               \
  }

class Type : public TypeBase {
 public:
  virtual bool IsSubtypeOf(const Type* supertype) const;

  std::string ToString() const;
  virtual std::string MangledName() const = 0;
  bool IsVoid() const { return IsAbstractName(VOID_TYPE_STRING); }
  bool IsNever() const { return IsAbstractName(NEVER_TYPE_STRING); }
  bool IsBool() const { return IsAbstractName(BOOL_TYPE_STRING); }
  bool IsConstexprBool() const {
    return IsAbstractName(CONSTEXPR_BOOL_TYPE_STRING);
  }
  bool IsVoidOrNever() const { return IsVoid() || IsNever(); }
  virtual std::string GetGeneratedTypeName() const = 0;
  virtual std::string GetGeneratedTNodeTypeName() const = 0;
  virtual bool IsConstexpr() const = 0;
  static const Type* CommonSupertype(const Type* a, const Type* b);
  void AddAlias(std::string alias) const { aliases_.insert(std::move(alias)); }

 protected:
  Type(TypeBase::Kind kind, const Type* parent)
      : TypeBase(kind), parent_(parent) {}
  const Type* parent() const { return parent_; }
  void set_parent(const Type* t) { parent_ = t; }
  int Depth() const;
  virtual std::string ToExplicitString() const = 0;

 private:
  bool IsAbstractName(const std::string& name) const;

  // If {parent_} is not nullptr, then this type is a subtype of {parent_}.
  const Type* parent_;
  mutable std::set<std::string> aliases_;
};

using TypeVector = std::vector<const Type*>;

class AbstractType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(AbstractType);
  const std::string& name() const { return name_; }
  std::string ToExplicitString() const override { return name(); }
  std::string MangledName() const override { return "AT" + name(); }
  std::string GetGeneratedTypeName() const override { return generated_type_; }
  std::string GetGeneratedTNodeTypeName() const override;
  bool IsConstexpr() const override {
    return name().substr(0, strlen(CONSTEXPR_TYPE_PREFIX)) ==
           CONSTEXPR_TYPE_PREFIX;
  }

 private:
  friend class Declarations;
  AbstractType(const Type* parent, const std::string& name,
               const std::string& generated_type)
      : Type(Kind::kAbstractType, parent),
        name_(name),
        generated_type_(generated_type) {}

  const std::string name_;
  const std::string generated_type_;
};

// For now, function pointers are restricted to Code objects of Torque-defined
// builtins.
class FunctionPointerType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(FunctionPointerType);
  std::string ToExplicitString() const override;
  std::string MangledName() const override;
  std::string GetGeneratedTypeName() const override {
    return parent()->GetGeneratedTypeName();
  }
  std::string GetGeneratedTNodeTypeName() const override {
    return parent()->GetGeneratedTNodeTypeName();
  }
  bool IsConstexpr() const override { return parent()->IsConstexpr(); }

  const TypeVector& parameter_types() const { return parameter_types_; }
  const Type* return_type() const { return return_type_; }

  friend size_t hash_value(const FunctionPointerType& p) {
    size_t result = base::hash_value(p.return_type_);
    for (const Type* parameter : p.parameter_types_) {
      result = base::hash_combine(result, parameter);
    }
    return result;
  }
  bool operator==(const FunctionPointerType& other) const {
    return parameter_types_ == other.parameter_types_ &&
           return_type_ == other.return_type_;
  }

 private:
  friend class Declarations;
  FunctionPointerType(const Type* parent, TypeVector parameter_types,
                      const Type* return_type)
      : Type(Kind::kFunctionPointerType, parent),
        parameter_types_(parameter_types),
        return_type_(return_type) {}

  const TypeVector parameter_types_;
  const Type* const return_type_;
};

bool operator<(const Type& a, const Type& b);
struct TypeLess {
  bool operator()(const Type* const a, const Type* const b) const {
    return *a < *b;
  }
};

class UnionType final : public Type {
 public:
  DECLARE_TYPE_BOILERPLATE(UnionType);
  std::string ToExplicitString() const override;
  std::string MangledName() const override;
  std::string GetGeneratedTypeName() const override {
    return "TNode<" + GetGeneratedTNodeTypeName() + ">";
  }
  std::string GetGeneratedTNodeTypeName() const override;

  bool IsConstexpr() const override {
    DCHECK_EQ(false, parent()->IsConstexpr());
    return false;
  }

  friend size_t hash_value(const UnionType& p) {
    size_t result = 0;
    for (const Type* t : p.types_) {
      result = base::hash_combine(result, t);
    }
    return result;
  }
  bool operator==(const UnionType& other) const {
    return types_ == other.types_;
  }

  base::Optional<const Type*> GetSingleMember() const {
    if (types_.size() == 1) {
      DCHECK_EQ(*types_.begin(), parent());
      return *types_.begin();
    }
    return base::nullopt;
  }

  const Type* Normalize() const {
    if (types_.size() == 1) {
      return parent();
    }
    return this;
  }

  bool IsSubtypeOf(const Type* other) const override {
    for (const Type* member : types_) {
      if (!member->IsSubtypeOf(other)) return false;
    }
    return true;
  }

  bool IsSupertypeOf(const Type* other) const {
    for (const Type* member : types_) {
      if (other->IsSubtypeOf(member)) {
        return true;
      }
    }
    return false;
  }

  void Extend(const Type* t) {
    if (const UnionType* union_type = UnionType::DynamicCast(t)) {
      for (const Type* member : union_type->types_) {
        Extend(member);
      }
    } else {
      if (t->IsSubtypeOf(this)) return;
      set_parent(CommonSupertype(parent(), t));
      for (const Type* member : types_) {
        if (member->IsSubtypeOf(t)) {
          types_.erase(member);
        }
      }
      types_.insert(t);
    }
  }

  static UnionType FromType(const Type* t) {
    const UnionType* union_type = UnionType::DynamicCast(t);
    return union_type ? UnionType(*union_type) : UnionType(t);
  }

 private:
  explicit UnionType(const Type* t) : Type(Kind::kUnionType, t), types_({t}) {}

  std::set<const Type*, TypeLess> types_;
};

inline std::ostream& operator<<(std::ostream& os, const Type* t) {
  os << t->ToString();
  return os;
}

class VisitResult {
 public:
  VisitResult() {}
  VisitResult(const Type* type, const std::string& variable)
      : type_(type), variable_(variable) {}
  const Type* type() const { return type_; }
  const std::string& variable() const { return variable_; }

 private:
  const Type* type_;
  std::string variable_;
};

class VisitResultVector : public std::vector<VisitResult> {
 public:
  VisitResultVector() : std::vector<VisitResult>() {}
  VisitResultVector(std::initializer_list<VisitResult> init)
      : std::vector<VisitResult>(init) {}
  TypeVector GetTypeVector() const {
    TypeVector result;
    for (auto& visit_result : *this) {
      result.push_back(visit_result.type());
    }
    return result;
  }
};

std::ostream& operator<<(std::ostream& os, const TypeVector& types);

struct NameAndType {
  std::string name;
  const Type* type;
};

typedef std::vector<NameAndType> NameAndTypeVector;

struct LabelDefinition {
  std::string name;
  NameAndTypeVector parameters;
};

typedef std::vector<LabelDefinition> LabelDefinitionVector;

struct LabelDeclaration {
  std::string name;
  TypeVector types;
};

typedef std::vector<LabelDeclaration> LabelDeclarationVector;

struct ParameterTypes {
  TypeVector types;
  bool var_args;
};

std::ostream& operator<<(std::ostream& os, const ParameterTypes& parameters);

struct Signature {
  const TypeVector& types() const { return parameter_types.types; }
  NameVector parameter_names;
  ParameterTypes parameter_types;
  const Type* return_type;
  LabelDeclarationVector labels;
  bool HasSameTypesAs(const Signature& other) const;
};

struct Arguments {
  VisitResultVector parameters;
  std::vector<Label*> labels;
};

std::ostream& operator<<(std::ostream& os, const Signature& sig);

bool IsAssignableFrom(const Type* to, const Type* from);
bool IsCompatibleSignature(const ParameterTypes& to, const TypeVector& from);

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_TYPES_H_
