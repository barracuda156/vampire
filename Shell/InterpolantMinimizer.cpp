/**
 * @file InterpolantMinimizer
 * Implements class InterpolantMinimizer.
 */

#include <fstream>
#include <sstream>

#include "Lib/Environment.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Renaming.hpp"
#include "Kernel/Term.hpp"

#include "Indexing/ClauseVariantIndex.hpp"

#include "Saturation/SWBSplitter.hpp"

#include "Interpolants.hpp"
#include "Options.hpp"

#include "InterpolantMinimizer.hpp"

namespace Shell
{

using namespace Indexing;

/**
 * Return minimized interpolant of @c refutation
 */
Formula* InterpolantMinimizer::getInterpolant(Unit* refutation)
{
  CALL("InterpolantMinimizer::getInterpolant");

  traverse(refutation);
  addAllFormulas();

  SMTSolverResult res;
  YicesSolver solver;
  YicesSolver::MinimizationResult mres = solver.minimize(_resBenchmark, costFunction(), res);

  DHSet<UnitSpec> slicedOff;

  if(mres==SMTSolver::FAIL) {
    cerr << "Minimization timed failed to find a satisfiable assignment, generating basic interpolant" << endl;
    goto just_generate_interpolant;
  }
  if(mres==SMTSolver::APPROXIMATE) {
    cerr << "Minimization gave approximate result" << endl;
  }

  if(_showStats) {
    env.beginOutput();
    env.out() << _statsPrefix << " cost: " <<res.assignment.get(costFunction()) << endl;
    env.endOutput();
  }

  collectSlicedOffNodes(res, slicedOff);

just_generate_interpolant:
  Formula* interpolant = Interpolants(&slicedOff).getInterpolant(refutation);
  return interpolant;
}

/**
 * Into @c acc add all units that are sliced off in the model given
 * by SMT solver in @c solverResult.
 */
void InterpolantMinimizer::collectSlicedOffNodes(SMTSolverResult& solverResult, DHSet<UnitSpec>& acc)
{
  CALL("InterpolantMinimizer::collectSlicedOffNodes");

  InfoMap::Iterator uit(_infos);
  while(uit.hasNext()) {
    UnitSpec unit;
    UnitInfo info;
    uit.next(unit, info);

    if(info.color!=COLOR_TRANSPARENT || !info.leadsToColor) {
      continue;
    }

    string uid = getUnitId(unit);

//    if(solverResult.assignment.get(pred(D, uid))=="true") LOG("Digest: " << unit.toString());

    SMTConstant sU = pred(S, uid);
    string val = solverResult.assignment.get(sU);
    if(val=="false") {
//      LOG("Non-sliced: " << unit.toString());
      continue;
    }
    ASS_EQ(val,"true");
    acc.insert(unit);
//    LOG("Sliced: " << unit.toString());
  }
}

/**
 * Add into @c _resBenchmark all formulas needed for interpolant minimization
 */
void InterpolantMinimizer::addAllFormulas()
{
  CALL("InterpolantMinimizer::getWholeFormula");

  InfoMap::Iterator uit(_infos);
  while(uit.hasNext()) {
    UnitSpec unit;
    UnitInfo info;
    uit.next(unit, info);

    if(info.color==COLOR_TRANSPARENT && info.leadsToColor) {
      addNodeFormulas(unit);
    }
  }

  addCostFormula();
}

/**
 * Add into @c _resBenchmark formulas related to @c u and to it's relation to
 * its parents.
 */
void InterpolantMinimizer::addNodeFormulas(UnitSpec u)
{
  CALL("InterpolantMinimizer::getNodeFormula");

  static ParentSummary psum;
  psum.reset();

  VirtualIterator<UnitSpec> pit = InferenceStore::instance()->getParents(u);
  while(pit.hasNext()) {
    UnitSpec par = pit.next();
    UnitInfo& info = _infos.get(par);
    if(!info.leadsToColor) {
      continue;
    }
    string parId = getUnitId(par);
    switch(info.color) {
    case COLOR_LEFT: psum.rParents.push(parId); break;
    case COLOR_RIGHT: psum.bParents.push(parId); break;
    case COLOR_TRANSPARENT: psum.gParents.push(parId); break;
    default: ASSERTION_VIOLATION;
    }
  }

  UnitInfo& uinfo = _infos.get(u);
  ASS_EQ(uinfo.color, COLOR_TRANSPARENT);

  string uId = getUnitId(u);

  if(uinfo.inputInheritedColor!=COLOR_TRANSPARENT) {
    //if unit has an inherited color, it must be input unit and therefore
    //cannot have any parents
    ASS(psum.rParents.isEmpty());
    ASS(psum.bParents.isEmpty());
    ASS(psum.gParents.isEmpty());

    addLeafNodePropertiesFormula(uId);
  }
  else {
    addNodeFormulas(uId, psum);
    addFringeFormulas(u);
  }



  if(_noSlicing || uinfo.isRefutation) {
    string comment;
    if(uinfo.isRefutation) {
      comment += "refutation";
    }
    _resBenchmark.addFormula(!pred(S,uId), comment);
  }

  //if formula is a parent of colored formula, we do not allow to have
  //opposite color in the trace.
  if(uinfo.isParentOfLeft) {
    _resBenchmark.addFormula(!pred(B,uId), "parent_of_left");
  }
  if(uinfo.isParentOfRight) {
    _resBenchmark.addFormula(!pred(R,uId), "parent_of_right");
  }

  addAtomImplicationFormula(u);
}

/**
 * Add formulas related tot he fringe of the node and to the digest
 *
 * These formulas aren't generated for leaves
 */
void InterpolantMinimizer::addFringeFormulas(UnitSpec u)
{
  CALL("InterpolantMinimizer::addFringeFormulas");

  string n = getUnitId(u);

  SMTFormula rcN = pred(RC, n);
  SMTFormula bcN = pred(BC, n);
  SMTFormula rfN = pred(RF, n);
  SMTFormula bfN = pred(BF, n);
  SMTFormula dN = pred(D, n);

  _resBenchmark.addFormula(dN -=- ((rcN & !rfN) | (bcN & !bfN)));

  UnitInfo& uinfo = _infos.get(u);

  if(uinfo.isRefutation) {
    _resBenchmark.addFormula(!rfN);
    _resBenchmark.addFormula(bfN);
    return;
  }

  SMTFormula rfRhs = SMTFormula::getTrue();
  SMTFormula bfRhs = SMTFormula::getTrue();
  USList::Iterator gsit(uinfo.transparentSuccessors);
  while(gsit.hasNext()) {
    UnitSpec succ = gsit.next();
    string succId = getUnitId(succ);

    SMTFormula rcS = pred(RC, succId);
    SMTFormula bcS = pred(BC, succId);
    SMTFormula rfS = pred(RF, succId);
    SMTFormula bfS = pred(BF, succId);

    rfRhs = rfRhs & ( rfS | rcS ) & !bcS;
    bfRhs = bfRhs & ( bfS | bcS ) & !rcS;
  }


  if(uinfo.rightSuccessors) {
    _resBenchmark.addFormula(!rfN);
  }
  else {
    _resBenchmark.addFormula(rfN -=- rfRhs);
  }

  if(uinfo.leftSuccessors) {
    _resBenchmark.addFormula(!bfN);
  }
  else {
    _resBenchmark.addFormula(bfN -=- bfRhs);
  }

}

/////////////////////////////////////////////////////////
// Generating the weight-minimizing part of the problem
//

/**
 * Class that splits a clause into components, faciliating also
 * sharing of the components
 */
class InterpolantMinimizer::ClauseSplitter : protected Saturation::SWBSplitter
{
public:
  ClauseSplitter() : _acc(0) {}

  /**
   * Into @c acc push clauses that correspond to components of @c cl.
   * The components are shared among calls to the function, so for
   * components that are variants of each other, the same result is
   * returned.
   */
  void getComponents(Clause* cl, ClauseStack& acc)
  {
    CALL("InterpolantMinimizer::ClauseSplitter::getComponents");
    ASS(!_acc);

    _acc = &acc;
    if(cl->length()==0) {
      handleNoSplit(cl);
    }
    else {
//      LOGV(cl->toString());
      ALWAYS(doSplitting(cl));
    }
    _acc = 0;
  }
protected:

  virtual void buildAndInsertComponents(Clause* cl, CompRec* comps,
      unsigned compCnt, bool firstIsMaster)
  {
    CALL("InterpolantMinimizer::ClauseSplitter::buildAndInsertComponents");

    for(unsigned i=0; i<compCnt; i++) {
      Clause* compCl = getComponent(comps[i].lits, comps[i].len);
      _acc->push(compCl);
    }
  }

  virtual bool handleNoSplit(Clause* cl)
  {
    CALL("InterpolantMinimizer::ClauseSplitter::handleNoSplit");

    _acc->push(getComponent(cl));
    return true;
  }

  virtual bool canSplitOut(Literal* lit) { return true; }
  virtual bool standAloneObligations() { return false; }
  virtual bool splittingAllowed(Clause* cl) { return true; }
private:

  Clause* getComponent(Literal** lits, unsigned len)
  {
    CALL("InterpolantMinimizer::ClauseSplitter::getComponent/2");

    if(len==1) {
      return getAtomComponent(lits[0], 0);
    }
    ClauseIterator cit = _index.retrieveVariants(lits, len);
    if(cit.hasNext()) {
      Clause* res = cit.next();
      ASS(!cit.hasNext());
      return res;
    }
    //here the input type and inference are just arbitrary, they'll never be used
    Clause* res = Clause::fromIterator(ArrayishObjectIterator<Literal**>(lits, len),
	Unit::AXIOM, new Inference(Inference::INPUT));
    res->incRefCnt();
    _index.insert(res);
    return res;
  }

  Clause* getComponent(Clause* cl)
  {
    CALL("InterpolantMinimizer::ClauseSplitter::getComponent/1");

    if(cl->length()==1) {
      return getAtomComponent((*cl)[0], cl);
    }

    ClauseIterator cit = _index.retrieveVariants(cl);
    if(cit.hasNext()) {
      Clause* res = cit.next();
      ASS(!cit.hasNext());
      return res;
    }
    _index.insert(cl);
    return cl;
  }

  /** cl can be 0 */
  Clause* getAtomComponent(Literal* lit, Clause* cl)
  {
    CALL("InterpolantMinimizer::ClauseSplitter::getAtomComponent");

    Literal* norm = lit->isNegative() ? Literal::oppositeLiteral(lit) : lit;
    norm = Renaming::normalize(norm);


    Clause* res;
    if(_atomIndex.find(norm, res)) {
      return res;
    }
    res = cl;
    if(!res) {
      res = Clause::fromIterator(getSingletonIterator(norm),
  	Unit::AXIOM, new Inference(Inference::INPUT));
    }
    ALWAYS(_atomIndex.insert(norm, res));
    return res;
  }

  ClauseVariantIndex _index;
  DHMap<Literal*,Clause*> _atomIndex;

  ClauseStack* _acc;
};

/**
 * Into @c atoms add IDs of components that appear in FormulaUnit @c u
 *
 * Currently we consider formulas to be one big component.
 */
void InterpolantMinimizer::collectAtoms(FormulaUnit* f, Stack<string>& atoms)
{
  CALL("InterpolantMinimizer::collectAtoms(FormulaUnit*...)");

  string key = f->formula()->toString();
  string id;
  if(!_formulaAtomIds.find(key, id)) {
    id = "f" + Int::toString(_formulaAtomIds.size());
    _formulaAtomIds.insert(key, id);
    unsigned weight = f->formula()->weight();
    _atomWeights.insert(id, weight);
    _unitsById.insert(id, UnitSpec(f));
  }
  atoms.push(id);
}

/**
 * Get ID of component @c cl
 */
string InterpolantMinimizer::getComponentId(Clause* cl)
{
  CALL("InterpolantMinimizer::getComponentId");

  string id;
  if(!_atomIds.find(cl, id)) {
    id = "c" + Int::toString(_atomIds.size());
    _atomIds.insert(cl, id);
    unsigned weight = cl->weight();
    _atomWeights.insert(id, weight);
    _unitsById.insert(id, UnitSpec(cl));
//    LOG(id<<" "<<weight<<"\t"<<cl->toString());
  }
 return id;
}

/**
 * Into @c atoms add IDs of components that appear in @c u
 */
void InterpolantMinimizer::collectAtoms(UnitSpec u, Stack<string>& atoms)
{
  CALL("InterpolantMinimizer::collectAtoms(UnitSpec...)");

  if(!u.isClause()) {
    collectAtoms(static_cast<FormulaUnit*>(u.unit()), atoms);
    return;
  }

  Clause* cl = u.cl();
  static ClauseStack components;
  components.reset();
  _splitter->getComponents(cl, components);
  ASS(components.isNonEmpty());
  ClauseStack::Iterator cit(components);
  while(cit.hasNext()) {
    Clause* comp = cit.next();
    atoms.push(getComponentId(comp));
  }
}

/**
 * Add formula implying the presence of components of @c u if
 * it appears in the digest into @c _resBenchmark
 */
void InterpolantMinimizer::addAtomImplicationFormula(UnitSpec u)
{
  CALL("InterpolantMinimizer::getAtomImplicationFormula");

  static Stack<string> atoms;
  atoms.reset();
  collectAtoms(u, atoms);

  string uId = getUnitId(u);

  SMTFormula cConj = SMTFormula::getTrue();
  Stack<string>::Iterator ait(atoms);
  while(ait.hasNext()) {
    string atom = ait.next();
    cConj = cConj & pred(V, atom);
  }

  string comment = "atom implications for " + u.toString();
  _resBenchmark.addFormula(pred(D, uId) --> cConj, comment);
}

/**
 * Add formula defining the cost function into @c _resBenchmark
 */
void InterpolantMinimizer::addCostFormula()
{
  CALL("InterpolantMinimizer::getCostFormula");

  SMTFormula costSum = SMTFormula::unsignedValue(0);

  WeightMap::Iterator wit(_atomWeights);
  while(wit.hasNext()) {
    string atom;
    unsigned weight;
    wit.next(atom, weight);

    Unit* unit = _unitsById.get(atom).unit();
    unsigned varCnt = unit->varCnt();

    if(_optTarget==OT_COUNT && weight>0) {
      weight = 1;
    }
    //**minimize the interpolant wrt the number of quantifiers
    if(_optTarget==OT_QUANTIFIERS){
      weight = varCnt;
    }

    SMTFormula atomExpr = SMTFormula::condNumber(pred(V, atom), weight);
    costSum = SMTFormula::add(costSum, atomExpr);
  }
  _resBenchmark.addFormula(SMTFormula::equals(costFunction(), costSum));
}

///////////////////////////////////////////
// Generating the SAT part of the problem
//

SMTConstant InterpolantMinimizer::pred(PredType t, string node)
{
  CALL("InterpolantMinimizer::pred");
  //Fake node is fictitious parent of gray nodes marked as colored in the TPTP.
  //We should never create predicates for these.
  ASS_NEQ(node, "fake_node");

  string n1;
  switch(t) {
  case R: n1 = "r"; break;
  case B: n1 = "b"; break;
  case G: n1 = "g"; break;
  case S: n1 = "s"; break;
  case RC: n1 = "rc"; break;
  case BC: n1 = "bc"; break;
  case RF: n1 = "rf"; break;
  case BF: n1 = "bf"; break;
  case D: n1 = "d"; break;
  case V: n1 = "v"; break;
  default: ASSERTION_VIOLATION;
  }
  SMTConstant res = SMTFormula::name(n1, node);
  _resBenchmark.declarePropositionalConstant(res);
  return res;
}

SMTConstant InterpolantMinimizer::costFunction()
{
  SMTConstant res = SMTFormula::name("cost");
  _resBenchmark.declareRealConstant(res);
  return res;
}

string InterpolantMinimizer::getUnitId(UnitSpec u)
{
  CALL("InterpolantMinimizer::getUnitId");

  string id = InferenceStore::instance()->getUnitIdStr(u);
//  _idToFormulas.insert(is, u); //the item might be already there from previous call to getUnitId
  return id;
}

/**
 * Add into @c _resBenchmark formulas stating uniqueness of trace colours
 * of node @c n.
 */
void InterpolantMinimizer::addDistinctColorsFormula(string n)
{
  CALL("InterpolantMinimizer::distinctColorsFormula");

  SMTFormula rN = pred(R, n);
  SMTFormula bN = pred(B, n);
  SMTFormula gN = pred(G, n);

  SMTFormula res = bN | rN | gN;
  res = res & ( rN --> ((!bN) & (!gN)) );
  res = res & ( bN --> ((!rN) & (!gN)) );
  res = res & ( gN --> ((!rN) & (!bN)) );

  _resBenchmark.addFormula(res);
}

/**
 * Add into @c _resBenchmark formulas related to digest and trace of node @c n
 * that are specific to a node which is parent of only gray formulas.
 */
void InterpolantMinimizer::addGreyNodePropertiesFormula(string n, ParentSummary& parents)
{
  CALL("InterpolantMinimizer::gNodePropertiesFormula");
  ASS(parents.rParents.isEmpty());
  ASS(parents.bParents.isEmpty());

  SMTFormula rParDisj = SMTFormula::getFalse();
  SMTFormula bParDisj = SMTFormula::getFalse();
  SMTFormula gParConj = SMTFormula::getTrue();

  Stack<string>::Iterator pit(parents.gParents);
  while(pit.hasNext()) {
    string par = pit.next();
    rParDisj = rParDisj | pred(R, par);
    bParDisj = bParDisj | pred(B, par);
    gParConj = gParConj & pred(G, par);
  }

  SMTFormula rN = pred(R, n);
  SMTFormula bN = pred(B, n);
  SMTFormula gN = pred(G, n);
  SMTFormula sN = pred(S, n);
  SMTFormula rcN = pred(RC, n);
  SMTFormula bcN = pred(BC, n);
//  SMTFormula dN = pred(D, n);


  _resBenchmark.addFormula(rcN -=- ((!sN) & rParDisj));
  _resBenchmark.addFormula(bcN -=- ((!sN) & bParDisj));

  _resBenchmark.addFormula(rParDisj-->!bParDisj);
  _resBenchmark.addFormula(bParDisj-->!rParDisj);
  _resBenchmark.addFormula((sN & rParDisj)-->rN);
  _resBenchmark.addFormula((sN & bParDisj)-->bN);
  _resBenchmark.addFormula((sN & gParConj)-->gN);
  _resBenchmark.addFormula( (!sN)-->gN );
//  _resBenchmark.addFormula( dN -=- ( (!sN) & !gParConj ) );
}

/**
 * Add properties for a leaf node which was marked as colored in the TPTP problem,
 * but doesn't contain any colored symbols
 */
void InterpolantMinimizer::addLeafNodePropertiesFormula(string n)
{
  CALL("InterpolantMinimizer::addLeafNodePropertiesFormula");

  SMTFormula gN = pred(G, n);
  SMTFormula sN = pred(S, n);
  SMTFormula dN = pred(D, n);

  _resBenchmark.addFormula(!sN);
  _resBenchmark.addFormula(gN);
  _resBenchmark.addFormula(dN);
}

/**
 * Add into @c _resBenchmark formulas related to digest and trace of node @c n
 * that are specific to a node which is parent of a colored formula.
 */
void InterpolantMinimizer::addColoredParentPropertiesFormulas(string n, ParentSummary& parents)
{
  CALL("InterpolantMinimizer::coloredParentPropertiesFormula");
  ASS_NEQ(parents.rParents.isNonEmpty(),parents.bParents.isNonEmpty());

  PredType parentType = parents.rParents.isNonEmpty() ? R : B;
  PredType oppositeType = (parentType==R) ? B : R;

  Stack<string>::Iterator gParIt(parents.gParents);

  SMTFormula gParNegConj = SMTFormula::getTrue();
  while(gParIt.hasNext()) {
    string par = gParIt.next();
    gParNegConj = gParNegConj & !pred(oppositeType, par);
  }

  SMTFormula parN = pred(parentType, n);
  SMTFormula gN = pred(G, n);
  SMTFormula sN = pred(S, n);
  SMTFormula rcN = pred(RC, n);
  SMTFormula bcN = pred(BC, n);
//  SMTFormula dN = pred(D, n);

  if(parentType==R) {
    _resBenchmark.addFormula(rcN -=- !sN);
    _resBenchmark.addFormula(!bcN);
  }
  else {
    ASS_EQ(parentType,B);
    _resBenchmark.addFormula(bcN -=- !sN);
    _resBenchmark.addFormula(!rcN);
  }

  _resBenchmark.addFormula(gParNegConj);
  _resBenchmark.addFormula(sN --> parN);
  _resBenchmark.addFormula((!sN) --> gN);
//  _resBenchmark.addFormula(dN -=- !sN);
}

/**
 * Add into @c _resBenchmark formulas related to digest and trace of node @c n, provided
 * @c n is not a leaf node.
 *
 * Formulas related to the cost function are added elsewhere.
 */
void InterpolantMinimizer::addNodeFormulas(string n, ParentSummary& parents)
{
  CALL("InterpolantMinimizer::propertiesFormula");

  addDistinctColorsFormula(n);

  if(parents.rParents.isEmpty() && parents.bParents.isEmpty()) {
    addGreyNodePropertiesFormula(n, parents);
  }
  else {
    addColoredParentPropertiesFormulas(n, parents);
  }
}

/////////////////////////
// Proof tree traversal
//

struct InterpolantMinimizer::TraverseStackEntry
{
  TraverseStackEntry(InterpolantMinimizer& im, UnitSpec u) : unit(u), _im(im)
  {
    CALL("InterpolantMinimizer::TraverseStackEntry::TraverseStackEntry");

    parentIterator = InferenceStore::instance()->getParents(u);

    //we don't create stack entries for already visited units,
    //so we must always be able to insert
    ALWAYS(im._infos.insert(unit, UnitInfo()));
    UnitInfo& info = getInfo();

    info.color = u.unit()->getColor();
    info.inputInheritedColor = u.unit()->inheritedColor();
    if(info.inputInheritedColor==COLOR_INVALID) {
      if(!parentIterator.hasNext()) {
	//this covers introduced name definitions
	info.inputInheritedColor = info.color;
      }
      else {
	info.inputInheritedColor = COLOR_TRANSPARENT;
      }
    }

    info.leadsToColor = info.color!=COLOR_TRANSPARENT || info.inputInheritedColor!=COLOR_TRANSPARENT;
  }

  /**
   * Extract the needed information on the relation between the current unit
   * and its premise @c parent
   */
  void processParent(UnitSpec parent)
  {
    CALL("InterpolantMinimizer::TraverseStackEntry::processParent");

    UnitInfo& info = getInfo();

    Color pcol = parent.unit()->getColor();
    if(pcol==COLOR_LEFT) {
//      if(info.state==HAS_RIGHT_PARENT) {
//	LOGV(parent.toString());
//	InferenceStore::instance()->outputProof(cout, unit.unit());
//      }
      ASS_NEQ(info.state, HAS_RIGHT_PARENT);
      info.state = HAS_LEFT_PARENT;
    }
    if(pcol==COLOR_RIGHT) {
      ASS_NEQ(info.state, HAS_LEFT_PARENT);
      info.state = HAS_RIGHT_PARENT;
    }

    UnitInfo& parInfo = _im._infos.get(parent);
    info.leadsToColor |= parInfo.leadsToColor;

    if(info.color==COLOR_LEFT) {
      parInfo.isParentOfLeft = true;
      USList::push(unit, parInfo.leftSuccessors);
    }
    else if(info.color==COLOR_RIGHT) {
      parInfo.isParentOfRight = true;
      USList::push(unit, parInfo.rightSuccessors);
    }
    else {
      ASS_EQ(info.color, COLOR_TRANSPARENT);
      USList::push(unit, parInfo.transparentSuccessors);
    }
  }

  /**
   * The returned reference is valid only before updating
   * InterpolantMinimizer::_infos
   */
  UnitInfo& getInfo()
  {
    return _im._infos.get(unit);
  }

  UnitSpec unit;
  /** Premises that are yet to be traversed */
  VirtualIterator<UnitSpec> parentIterator;

  InterpolantMinimizer& _im;
};

/**
 * Traverse through the proof graph of @c refutationClause and
 * record everything that is necessary for generating the
 * minimization problem
 */
void InterpolantMinimizer::traverse(Unit* refutationUnit)
{
  CALL("InterpolantMinimizer::traverse");

  UnitSpec refutation=UnitSpec(refutationUnit);

  static Stack<TraverseStackEntry> stack;
  stack.reset();

  stack.push(TraverseStackEntry(*this, refutation));
  stack.top().getInfo().isRefutation = true;
  while(stack.isNonEmpty()) {
    TraverseStackEntry& top = stack.top();
    if(top.parentIterator.hasNext()) {
      UnitSpec parent = top.parentIterator.next();

      if(!_infos.find(parent)) {
	stack.push(TraverseStackEntry(*this, parent));
      }
      else {
	top.processParent(parent);
      }
    }
    else {
      if(stack.size()>1){
	TraverseStackEntry& child = stack[stack.size()-2];
	child.processParent(stack.top().unit);
      }
      stack.pop();
    }
  }

}

///////////////////////////////
// Construction & destruction
//

/**
 * Create InterpolantMinimizer object
 *
 * @param minimizeComponentCount If true, we minimize the number of distinct
 * components in the interpolant, otherwise we minimize the sum of the weight
 * of the distinct components.
 *
 * @param noSlicing If true, we forbid all slicing of proof nodes. This simulates
 * the original algorithm which didn't use minimization.
 *
 * @param showStats Value of the cost function is output
 *
 * @param starsPrefix The value of the cost function is output on line starting
 * with statsPrefix + " cost: "
 */
InterpolantMinimizer::InterpolantMinimizer(OptimizationTarget target, bool noSlicing,
    bool showStats, string statsPrefix)
: _optTarget(target), _noSlicing(noSlicing),
  _showStats(showStats), _statsPrefix(statsPrefix)
{
  CALL("InterpolantMinimizer::InterpolantMinimizer");

  _splitter = new ClauseSplitter();
}

InterpolantMinimizer::~InterpolantMinimizer()
{
  CALL("InterpolantMinimizer::~InterpolantMinimizer");

  delete _splitter;
}


}
