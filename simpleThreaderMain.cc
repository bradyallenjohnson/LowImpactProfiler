/*
 * simpleThreaderMain.cc
 *
 *  Created on: Jul 18, 2012
 *      Author: ebrjohn
 */

#include <string>
#include <iostream>

#include <pthread.h>
#include <time.h>
#include <stdint.h> // uint32_t et al
#include <stdlib.h> // exit()

#include <errno.h>
#include <string.h>  // strerror()

#include <sys/time.h>      // rlimit()
#include <sys/resource.h>  // rlimit()

#include <CmdLineParser.h>

#include "LowImpactProfiler.h"

using namespace std;

const string ARG_NUM_THREADS    = "-t";
const string ARG_SLEEP_MICROS   = "-s";
const string ARG_NUM_LOOPS      = "-l";
const string ARG_LIP_LOCKING    = "-b";

struct ConfigInput
{
  // Command line options
  uint32_t numThreads;
  uint32_t sleepMicros;
  uint32_t numLoops;
  bool lipLocking;

  ConfigInput() : numThreads(3), sleepMicros(500), numLoops(10), lipLocking(false) {}
};

void loadCmdLine(CmdLineParser &clp)
{
  clp.setMainHelpText("A simple threaded application to test the Low Impact Profiler");

  //
  // Optional args
  //
     // Number of Threads
  clp.addCmdLineOption(new CmdLineOptionInt(ARG_NUM_THREADS,
                                            string("Number of threads to create"),
                                            3));
  // us sleep time
  clp.addCmdLineOption(new CmdLineOptionInt(ARG_SLEEP_MICROS,
                                            string("Time in microseconds to sleep in each loop"),
                                            500));
  // Number of loops
  clp.addCmdLineOption(new CmdLineOptionInt(ARG_NUM_LOOPS,
                                            string("Number of thread iteration loops"),
                                            10));
  // Number of Threads
  clp.addCmdLineOption(new CmdLineOptionFlag(ARG_LIP_LOCKING,
                                             string("Use locking checkpoints"),
                                             false));
}


bool parseCommandLine(int argc, char **argv, CmdLineParser &clp, ConfigInput &config)
{
  if(!clp.parseCmdLine(argc, argv))
  {
    clp.printUsage();
    return false;
  }

  config.numThreads   =  ((CmdLineOptionInt*)   clp.getCmdLineOption(ARG_NUM_THREADS))->getValue();
  config.sleepMicros  =  ((CmdLineOptionInt*)   clp.getCmdLineOption(ARG_SLEEP_MICROS))->getValue();
  config.numLoops     =  ((CmdLineOptionInt*)   clp.getCmdLineOption(ARG_NUM_LOOPS))->getValue();
  config.lipLocking   =  ((CmdLineOptionFlag*)  clp.getCmdLineOption(ARG_LIP_LOCKING))->getValue();

  // Check that the number of threads configured is not too high for the system
  struct rlimit rlimit;
  if(getrlimit(RLIMIT_NPROC, &rlimit) != 0)
  {
    cerr << "ERROR in getrlimit()" << endl;
  }

  if(config.numThreads >= rlimit.rlim_cur)
  {
    cerr << "Number of threads specified is higher than allowed by the system limit: " << rlimit.rlim_cur << endl;
    return false;
  }

  return true;
}

void *threadEntryPoint(void *userData)
{
  ConfigInput *config = (ConfigInput*) userData;

  CHECKPOINT(0);

  for(int i = 0; i < config->numLoops; ++i)
  {
    CHECKPOINT(1);
    // This one will tell us how long a single checkpoint takes
    CHECKPOINT(2);

    usleep(config->sleepMicros);

    CHECKPOINT(3);
  }

  CHECKPOINT(4);

  return NULL;
}

void printTime(const char *msg, pthread_t threadId = 0)
{
  struct timespec now;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);

  cout << msg << " at [sec, ns] = [" << now.tv_sec
       << ", " << now.tv_nsec << "]";

  if(threadId)
  {
    cout << ", threadId [" << threadId << "]";
  }

  cout << endl;
}

int main(int argc, char **argv)
{
  // Handle the Command line args
  CmdLineParser clp;
  loadCmdLine(clp);

  ConfigInput input;
  if(!parseCommandLine(argc, argv, clp, input))
  {
    cerr << "Error parsing command line arguments, exiting" << endl;
    return 1;
  }

  cout << "\nThe threads should take at least (numLoops * microSleepTime) = (" << input.numLoops
       << " * " << input.sleepMicros
       << ") = (" << (input.numLoops * input.sleepMicros)
       << ") microSeconds\n";

  printTime("\nStarting Threads");

  Checkpoint::initialize(input.numThreads, input.lipLocking);

  pthread_t threadIds[input.numThreads];
  for(int i = 0; i < input.numThreads; ++i)
  {
    int retval(pthread_create(&threadIds[i], NULL, threadEntryPoint, (void*) &input));
    if(retval != 0)
    {
      cerr << "ERROR creating threads: pthread_create() returned error [" << retval << "] " << strerror(retval)
           << ", exiting"
           << endl;
      exit(1);
    }
    printTime("Created thread", threadIds[i]);
  }

  printTime("\nAll threads created");

  for(int i = 0; i < input.numThreads; ++i)
  {
    pthread_join(threadIds[i], NULL);
    printTime("Thread joined", threadIds[i]);
  }

  printTime("\nAll threads finished");

  Checkpoint::instance()->dump();

  printTime("All finished");

  return 0;
}
