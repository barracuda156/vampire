/**
 * @file vclausify.cpp. Implements the main function for a separate executable that only performs clausification.
 */

#include <string>
#include <iostream>

#include "Debug/Tracer.hpp"

#include "Lib/Exception.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"
#include "Lib/Random.hpp"
#include "Lib/ScopedPtr.hpp"
#include "Lib/Set.hpp"
#include "Lib/Stack.hpp"
#include "Lib/TimeCounter.hpp"
#include "Lib/Timer.hpp"

#include "Lib/List.hpp"
#include "Lib/Vector.hpp"
#include "Lib/System.hpp"
#include "Lib/Metaiterators.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Term.hpp"

#include "Indexing/TermSharing.hpp"

#include "Inferences/InferenceEngine.hpp"
#include "Inferences/TautologyDeletionISE.hpp"

#include "Shell/CommandLine.hpp"
#include "Shell/Options.hpp"
#include "Shell/Property.hpp"
#include "Shell/Preprocess.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/TPTP.hpp"
#include "Shell/UIHelper.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

using namespace Shell;
using namespace SAT;
using namespace Saturation;
using namespace Inferences;

UnitList* globUnitList=0;

/**
 * Return value is non-zero unless we were successful.
 *
 * Being successful for modes that involve proving means that we have
 * either found refutation or established satisfiability.
 *
 *
 * If Vampire was interupted by a SIGINT, value 3 is returned,
 * and in case of other signal we return 2. For implementation
 * of these return values see Lib/System.hpp.
 *
 * In case Vampire was terminated by the timer, return value is
 * uncertain (but definitely not zero), probably it will be 134
 * (we terminate by a call to the @b abort() function in this case).
 */
int vampireReturnValue = 1;

ClauseIterator getProblemClauses()
{
  CALL("getInputClauses");


  UnitList* units=UIHelper::getInputUnits();

  TimeCounter tc2(TC_PREPROCESSING);

  env.statistics->phase=Statistics::PROPERTY_SCANNING;
  ScopedPtr<Property> property(Property::scan(units));
  Preprocess prepro(*property,*env.options);
  //phases for preprocessing are being set inside the proprocess method
  prepro.preprocess(units);

  globUnitList=units;

  return pvi( getStaticCastIterator<Clause*>(UnitList::Iterator(units)) );
}

void clausifyMode()
{
  CALL("clausifyMode()");

  CompositeISE simplifier;
  simplifier.addFront(ImmediateSimplificationEngineSP(new TrivialInequalitiesRemovalISE()));
  simplifier.addFront(ImmediateSimplificationEngineSP(new TautologyDeletionISE()));
  simplifier.addFront(ImmediateSimplificationEngineSP(new DuplicateLiteralRemovalISE()));

  ClauseIterator cit = getProblemClauses();
  env.beginOutput();
  while (cit.hasNext()) {
    Clause* cl=cit.next();
    cl=simplifier.simplify(cl);
    if(!cl) {
      continue;
    }
    env.out() << TPTP::toString(cl) << "\n";
  }
  env.endOutput();

  //we have successfully output all clauses, so we'll terminate with zero return value
  vampireReturnValue=0;
} // clausifyMode

void explainException (Exception& exception)
{
  env.beginOutput();
  exception.cry(env.out());
  env.endOutput();
} // explainException

/**
 * The main function.
  * @since 03/12/2003 many changes related to logging
  *        and exception handling.
  * @since 10/09/2004, Manchester changed to use knowledge bases
  */
int main(int argc, char* argv [])
{
  CALL ("main");

  System::registerArgv0(argv[0]);
  System::setSignalHandlers();
   // create random seed for the random number generation
  Lib::Random::setSeed(123456);

  try {
    env.options->setMode(Options::MODE_CLAUSIFY);

    // read the command line and interpret it
    Shell::CommandLine cl(argc,argv);
    cl.interpret(*env.options);

    if(env.options->mode()!=Options::MODE_CLAUSIFY) {
      USER_ERROR("Only the \"clausify\" mode is supported");
    }

    Allocator::setMemoryLimit(env.options->memoryLimit()*1048576ul);
    Lib::Random::setSeed(env.options->randomSeed());

    clausifyMode();

  }
#if VDEBUG
  catch (Debug::AssertionViolationException& exception) {
    reportSpiderFail();
  }
#endif
  catch (UserErrorException& exception) {
    reportSpiderFail();
    explainException(exception);
  }
  catch (Exception& exception) {
    reportSpiderFail();
    env.beginOutput();
    explainException(exception);
    env.statistics->print(env.out());
    env.endOutput();
  }
  catch (std::bad_alloc& _) {
    reportSpiderFail();
    env.beginOutput();
    env.out() << "Insufficient system memory" << '\n';
    env.endOutput();
  }
//   delete env.allocator;

  return vampireReturnValue;
} // main

