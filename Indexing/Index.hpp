/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file Indexing/Index.hpp
 * Defines abstract Index class and some other auxiliary classes.
 */

#ifndef __Indexing_Index__
#define __Indexing_Index__

#include "Forwards.hpp"
#include "Debug/Output.hpp"

#include "Lib/Event.hpp"
#include "Kernel/Clause.hpp"
#include "Lib/Exception.hpp"
#include "Lib/VirtualIterator.hpp"
#include "Saturation/ClauseContainer.hpp"
#include "ResultSubstitution.hpp"
#include "Kernel/UnificationWithAbstraction.hpp"

#include "Lib/Allocator.hpp"
/**
 * Indices are parametrized by a LeafData, i.e. the bit of data you want to store in the index.
 * Each leaf data must have a key to store at the leave. The Key can currently be either a Literal* or a TypedTermList.
 * A LeafData must have a function  `<Key> key() const;` returns the key, and must have comparison operators (<=,<,>=,>,!=,==) implemented.
 * See e.g. TermLiteralClause below for examples.
 */
namespace Indexing
{
using namespace Kernel;
using namespace Lib;
using namespace Saturation;

struct LiteralClause 
{
  LiteralClause() {}
  Literal* const& key() const
  { return literal; }

  LiteralClause(Literal* lit, Clause* cl)
    : literal(lit) 
    , clause(cl)
  { ASS(cl); ASS(literal) }

  LiteralClause(Clause* cl, Literal* lit)
    : LiteralClause(lit, cl) {}

private:
  auto asTuple() const
  { return std::make_tuple(clause->number(), literal->getId()); }
public:

  IMPL_COMPARISONS_FROM_TUPLE(LiteralClause)

  Literal* literal;
  Clause* clause;

  friend std::ostream& operator<<(std::ostream& out, LiteralClause const& self)
  { return out << "{ " << outputPtr(self.clause) << ", " << outputPtr(self.literal) << " }"; }
};

template<class Value>
struct TermWithValue {
  TypedTermList term;
  Value value;

  TermWithValue() {}

  TermWithValue(TypedTermList term, Value v)
    : term(term)
    , value(std::move(v))
  {}

  TypedTermList const& key() const { return term; }

  auto asTuple() const
  { return std::tie(term, value); }

  IMPL_COMPARISONS_FROM_TUPLE(TermWithValue)

  friend std::ostream& operator<<(std::ostream& out, TermWithValue const& self)
  { return out << self.asTuple(); }
};

class TermWithoutValue : public TermWithValue<std::tuple<>> 
{
public:
  TermWithoutValue(TypedTermList t) 
    : TermWithValue(t, std::make_tuple()) 
  { }
};

struct TermLiteralClause 
{
  Clause* clause;
  Literal* literal;
  TypedTermList term;

  TermLiteralClause() : clause(nullptr), literal(nullptr), term() {}

  TermLiteralClause(TypedTermList t, Literal* l, Clause* c)
    : clause(c), literal(l), term(t) 
  { ASS(l); ASS(c) }

  TypedTermList const& key() const { return term; }

  auto  asTuple() const
  { return std::make_tuple(clause->number(), literal->getId(), term); }

  IMPL_COMPARISONS_FROM_TUPLE(TermLiteralClause)

  friend std::ostream& operator<<(std::ostream& out, TermLiteralClause const& self)
  { return out << "("
               << self.term << ", "
               << self.literal
               << outputPtr(self.clause)
               << ")"; }
};

/**
 * Class of objects which contain results of term queries.
 */
template<class Unifier, class Data>
struct QueryRes
{
  Unifier unifier;
  Data const* data;

  QueryRes() {}
  QueryRes(Unifier unifier, Data const* data) 
    : unifier(std::move(unifier))
    , data(std::move(data)) {}

  friend std::ostream& operator<<(std::ostream& out, QueryRes const& self)
  { 
    return out 
      << "{ data: " << self.data()
      << ", unifier: " << self.unifier
      << "}";
  }
};

template<class Unifier, class Data>
QueryRes<Unifier, Data> queryRes(Unifier unifier, Data const* d) 
{ return QueryRes<Unifier, Data>(std::move(unifier), std::move(d)); }

class Index
{
public:

  virtual ~Index();

  void attachContainer(ClauseContainer* cc);
protected:
  Index() {}

  void onAddedToContainer(Clause* c)
  { handleClause(c, true); }
  void onRemovedFromContainer(Clause* c)
  { handleClause(c, false); }

  virtual void handleClause(Clause* c, bool adding) {}

  //TODO: postponing index modifications during iteration (methods isBeingIterated() etc...)

private:
  SubscriptionData _addedSD;
  SubscriptionData _removedSD;
};

};
#endif /*__Indexing_Index__*/
