/*
 * simpleThreaderMain.cc
 *
 *  Created on: Jul 18, 2012
 *      Author: ebrjohn
 */

#include <string>
#include <iostream>
#include <sstream>

#include <time.h>
#include <stdint.h> // uint32_t et al
#include <sys/time.h>

#include <CmdLineParser.h>

#include "LowImpactProfiler.h"

using namespace std;

const string ARG_SLEEP_MICROS   = "-s";
const string ARG_NUM_LOOPS      = "-l";
const string ARG_LIP_LOCKING    = "-b";

struct ConfigInput
{
  // Command line options
  uint32_t sleepMicros;
  struct timespec sleepTime;
  uint32_t numLoops;
  bool lipLocking;

  ConfigInput() : sleepMicros(500), numLoops(10), lipLocking(false) {}
};

void loadCmdLine(CmdLineParser &clp)
{
  clp.setMainHelpText("A simple NON-threaded application to test the Low Impact Profiler");

  //
  // Optional args
  //

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

  config.sleepMicros  =  ((CmdLineOptionInt*)   clp.getCmdLineOption(ARG_SLEEP_MICROS))->getValue();
  config.numLoops     =  ((CmdLineOptionInt*)   clp.getCmdLineOption(ARG_NUM_LOOPS))->getValue();
  config.lipLocking   =  ((CmdLineOptionFlag*)  clp.getCmdLineOption(ARG_LIP_LOCKING))->getValue();

  if(config.sleepMicros >= 1000000)
  {
    config.sleepTime.tv_sec = config.sleepMicros/1000000;
    config.sleepMicros -= config.sleepTime.tv_sec * 1000000;
    config.sleepTime.tv_nsec = config.sleepMicros * 1000;
  }
  else
  {
    config.sleepTime.tv_sec = 0;
    config.sleepTime.tv_nsec = config.sleepMicros * 1000;
  }
  cout << "SleepTime [" << config.sleepTime.tv_sec << ", " << config.sleepTime.tv_nsec << "]\n";

  return true;
}

void doWork(ConfigInput &config)
{
  struct timespec sleepTime = config.sleepTime;

  CHECKPOINT(0);

  for(int i = 0; i < config.numLoops; ++i)
  {
    CHECKPOINT(1);
    // This one will tell us how long a single checkpoint takes
    CHECKPOINT(2);

    //usleep(config->sleepMicros);
    if(nanosleep(&sleepTime, NULL))
    {
      cout << "nanosleep() failed\n";
    }

    CHECKPOINT(3);
  }

  CHECKPOINT(4);
}

uint64_t diffTimes(struct timespec &start, struct timespec &end)
{
  if(start.tv_sec == end.tv_sec)
  {
    return end.tv_nsec - start.tv_nsec;
  }
  else if(start.tv_sec < end.tv_sec)
  {
    uint64_t nsecs = (end.tv_sec - start.tv_sec) * 1000000000;
    return nsecs + end.tv_nsec - start.tv_nsec;
  }
  else
  {
    // this is actually an error
    return 0;
  }
}

void getCycles(struct timespec &now)
{
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
}

// Given 2 timespec structs which have seconds and nano seconds,
// return the difference in nano seconds
void printTime(const char *msg, struct timespec &start, struct timespec &end)
{
  cout << msg << " [start, end] (sec,ns) = [(" << start.tv_sec
       << ", " << start.tv_nsec
       << "), (" << end.tv_sec
       << ", " << end.tv_nsec << ")]"
       << " diff ns = [" << diffTimes(start, end) << "]"
       << endl;
}

void printTime(const char *msg)
{
  struct timespec now;
  getCycles(now);

  cout << msg << " at [sec, ns] = [" << now.tv_sec
       << ", " << now.tv_nsec << "]"
       << endl;
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

  struct timespec start, end;

  cout << "\nThe execution should take at least (numLoops * microSleepTime) = (" << input.numLoops
       << " * " << input.sleepMicros
       << ") = (" << (input.numLoops * input.sleepMicros)
       << ") microSeconds\n";

  printTime("\nInitializing Profiler");

  // Initializing with 0 means not multi-threaded
  Checkpoint::initialize(0, input.lipLocking);

  getCycles(start);

  doWork(input);

  getCycles(end);

  printTime("\nWork finished", start, end);

  Checkpoint::instance()->dump(true, true, true, true);

  cout << "Now for minimal checkpoints" << endl;

  ostringstream cpStream;
  Checkpoint::instance()->dump(cpStream, false);
  Checkpoint::destroy();
  cout << cpStream.str() << endl;

  printTime("All finished");

  return 0;
}
