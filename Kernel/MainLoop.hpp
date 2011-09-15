/**
 * @file MainLoop.hpp
 * Defines class MainLoop.
 */

#ifndef __MainLoop__
#define __MainLoop__

#include "Forwards.hpp"

#include "Lib/Environment.hpp"
#include "Lib/Exception.hpp"

#include "Shell/Statistics.hpp"

namespace Shell {
  class Property;
};

namespace Kernel {

using namespace Lib;
using namespace Inferences;
using namespace Shell;

struct MainLoopResult
{
  typedef Statistics::TerminationReason TerminationReason;

  MainLoopResult(TerminationReason reason)
  : terminationReason(reason) {}
  MainLoopResult(TerminationReason reason, Clause* ref)
  : terminationReason(reason), refutation(ref) {}

  void updateStatistics();

  TerminationReason terminationReason;
  Clause* refutation;
};



class MainLoop {
public:
  MainLoop(Problem& prb, const Options& opt) : _prb(prb), _opt(opt) {}
  virtual ~MainLoop() {}


  MainLoopResult run();
  static MainLoop* createFromOptions(Problem& prb, const Options& opt);

  /**
   * A struct that is thrown as an exception when a refutation is found
   * during the main loop.
   */
  struct RefutationFoundException : public ThrowableBase
  {
    RefutationFoundException(Clause* ref) : refutation(ref)
    {
      CALL("MainLoop::RefutationFoundException::RefutationFoundException");
      ASS(isRefutation(ref));
    }

    Clause* refutation;
  };

  /**
   * A struct that is thrown as an exception when a refutation is found
   * during the main loop.
   */
  struct MainLoopFinishedException : public ThrowableBase
  {
    MainLoopFinishedException(const MainLoopResult& res) : result(res)
    {
    }

    MainLoopResult result;
  };

  /**
   * Return the problem that the solving algorithm is being run on
   */
  const Problem& getProblem() const { return _prb; }

  /**
   * Get options specifying strategy for the solving algorithm
   */
  const Options& getOptions() const { return _opt; }

protected:
  enum ClauseReportType
  {
    CRT_ACTIVE,
    CRT_PASSIVE,
    CRT_NEW,
    CRT_NEW_PROPOSITIONAL
  };
  void reportClause(ClauseReportType type, Clause* cl);
  void reportClause(ClauseReportType type, string clString);

  static bool isRefutation(Clause* cl);
  static ImmediateSimplificationEngine* createISE(Problem& prb, const Options& opt);

  /**
   * This function is called after all initialization of the main loop
   * algorithm is done (especially when all the indexes are in place).
   *
   * In this function the implementing class should retrieve clauses
   * from the Problem object @c _prb and load them into the algorithm.
   *
   * In former versions the action taken by this method corresponded
   * to the function addInputClauses().
   */
  virtual void init() = 0;

  /**
   * The actual run of the solving algorithm should be implemented in
   * this function.
   */
  virtual MainLoopResult runImpl() = 0;

  Problem& _prb;

  /**
   * Options that represent the strategy used by the current main loop
   */
  const Options& _opt;
};

}

#endif // __MainLoop__
