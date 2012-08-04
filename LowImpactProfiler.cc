
#include <iostream>

#include <pthread.h>
#include <string.h> // memset
#include <stdint.h> // uint32_t et al
#include <time.h>   // clock_gettime() et al

#include "LowImpactProfiler.h"

// Initialize static class variables
Checkpoint *Checkpoint::instance_ = 0;
bool Checkpoint::useLocking_ = true;
const string Checkpoint::SECOND_STR    = "Seconds";
const string Checkpoint::MICRO_SEC_STR = "MicroSec";
const string Checkpoint::NANO_SEC_STR  = "NanoSec";

using namespace std;

// static
// Singleton method to create/retrieve Checkpoint object
// Must be called the first time from somewhere where all cores will execute, like main().
// If its called for the first time from the DataPlane() the ControlPlane() core 0 wont hit it.
void Checkpoint::initialize(uint32_t numCores, bool useLocking) /* default values: MAX_CORES, true */
{
  Checkpoint::useLocking_ = useLocking;
  if(instance_ == 0)
  {
    instance_ = new Checkpoint(numCores);
  }
}

// static
// Singleton method to create/retrieve Checkpoint object
// Must be called the first time from somewhere where all cores will execute, like main().
// If its called for the first time from the DataPlane() the ControlPlane() core 0 wont hit it.
Checkpoint* Checkpoint::instance()
{
  if(instance_ == 0)
  {
    Checkpoint::initialize();
  }

  return instance_;
}

// protected
Checkpoint::Checkpoint(uint32_t numCores) :
    numCores_(numCores),
    isActive_(true),
    threadIdCounter_(0)
{
  clockid_t clockId;
  int retval(clock_getcpuclockid(0, &clockId));
  if(retval != 0)
  {
	  cout << "NOTICE: clock_getcpuclock() returned error: " << retval
           << "\nThis may be an indication that the time on different CPU cores may not be consistent"
           << endl;
  }

  pthread_mutex_init(&checkpointLock_, NULL); // initialize it even if !useLocking_
  pthread_rwlock_init(&threadIdMapRwLock_, NULL);

  // Set the initial vector size to numCores, it will grow dynamically if needed
  coreCpInfoVector_.reserve(numCores);
}

// private
Checkpoint::CoreCheckpointInfo *Checkpoint::getCoreCpInfo()
{
  pthread_t threadId(pthread_self());
  CoreCheckpointInfo *coreCpInfo(NULL);

  pthread_rwlock_rdlock(&threadIdMapRwLock_);
  Checkpoint::ThreadIdMapType::iterator iter = threadIdMap_.find(threadId);

  if(iter != threadIdMap_.end())
  {
    coreCpInfo = &(coreCpInfoVector_[iter->second]);
    pthread_rwlock_unlock(&threadIdMapRwLock_);
  }
  else
  {
    // release the read lock
    pthread_rwlock_unlock(&threadIdMapRwLock_);

    // get a write lock, since we'll have to modify the map
    pthread_rwlock_wrlock(&threadIdMapRwLock_);
      // Combine the threadIdMap_ and coreCpInfoVector_ into just one map
      threadIdMap_[threadId] = threadIdCounter_;
      coreCpInfoVector_[threadIdCounter_] = CoreCheckpointInfo();
      coreCpInfo = &(coreCpInfoVector_[threadIdCounter_++]);
    pthread_rwlock_unlock(&threadIdMapRwLock_);
  }

  return coreCpInfo;
}


// Method to calculate current checkpoint information
void Checkpoint::checkpoint(int checkpoint)
{
  if(__unlikely(!isActive_)) {
    return;
  }

  // Not checking coreNum nor checkpoint for performance reasons

  CoreCheckpointInfo *coreCp  (  getCoreCpInfo() );
  CheckpointInfo *currentCp   (  &(coreCp->checkpoints_[checkpoint]) );
  CheckpointInfo *previousCp  (  &(coreCp->checkpoints_[coreCp->lastCheckpointHit_]) );
  coreCp->lastCheckpointHit_  =  checkpoint;

  if(__unlikely(useLocking_)) {
    pthread_mutex_lock(&checkpointLock_);
  }

  // calculate and store deltas
  ++(currentCp->iterations_);
  currentCp->previousCycles_ = getCycles();
  currentCp->totalCycles_   += (currentCp->previousCycles_ - previousCp->previousCycles_);

  if(__unlikely(useLocking_)) {
	  pthread_mutex_unlock(&checkpointLock_);
  }
}

// Dump all the checkpoint information
void Checkpoint::dump()
{
  cout << "Number of Cores [configured, used] = [" << numCores_
       << ", " << threadIdCounter_
       << "]"
       << endl;

  struct timespec resolution;
  clock_getres(CLOCK_THREAD_CPUTIME_ID, &resolution);
  cout << "Timer resolution in nanoseconds [" << resolution.tv_nsec << "]" << endl;

  if(useLocking_)
  {
    pthread_mutex_lock(&checkpointLock_);
  }

  CoreCheckpointInfo *coreCp(&(coreCpInfoVector_[0]));
  CheckpointInfo totalCpAvg[MAX_CHK];
  uint32_t numCpHits[MAX_CHK];
  memset(numCpHits, 0, sizeof(uint32_t)*MAX_CHK);

  for(int core = 0; core < threadIdCounter_; coreCp = &(coreCpInfoVector_[++core]))
  {
    bool coreUsed(false);
    CheckpointInfo *currentCp(coreCp->checkpoints_);
    for(int chkPoint=0; chkPoint < MAX_CHK; currentCp = &(coreCp->checkpoints_[++chkPoint]))
    {
      if(currentCp->iterations_ != 0)
      {
        coreUsed = true;
      }
    }

    if(!coreUsed)
    {
      //cout << "CORE [" << core << "] No checkpoints hit on this core, skipping\n" << endl;
      continue;
    }

    currentCp = coreCp->checkpoints_;
    for(int chkPoint=0; chkPoint < MAX_CHK; currentCp = &(coreCp->checkpoints_[++chkPoint]))
    {
      uint64_t avgCycles(0);

      if(currentCp->iterations_ != 0)
      {
        // Avoiding possible divide by zero
        avgCycles = currentCp->totalCycles_/currentCp->iterations_;
        totalCpAvg[chkPoint] += currentCp;
        numCpHits[chkPoint]++;
      }

      const char *unitPtr(Checkpoint::NANO_SEC_STR.c_str());
      uint64_t totalCycles(currentCp->totalCycles_);

      if(avgCycles > 999999 && totalCycles > 9999999)
      {
        totalCycles /= 1000000000;
        avgCycles   /= 1000000000;
        unitPtr = Checkpoint::SECOND_STR.c_str();
      }
      else if(avgCycles > 9999 && totalCycles > 99999)
      {
   	    totalCycles /= 1000;
        avgCycles   /= 1000;
   	    unitPtr = Checkpoint::MICRO_SEC_STR.c_str();
      }

      cout << "CORE [" << core
           << "] Checkpoint [" << chkPoint
           << "] Iterations [" << currentCp->iterations_
           << "] Time [Unit,Avg,Total] = [" << unitPtr
           << ", " << avgCycles
           << ", " << totalCycles << "]\n";
    }
    cout << endl;
  }

  // Now print the averages
  for(int i = 0; i < MAX_CHK; ++i)
  {
    CheckpointInfo *avgCp = &(totalCpAvg[i]);
    if(numCpHits[i] > 1)
    {
      avgCp->totalCycles_ = avgCp->totalCycles_/numCpHits[i];
      avgCp->iterations_ = avgCp->iterations_/numCpHits[i];

      const char *unitPtr(Checkpoint::NANO_SEC_STR.c_str());
      uint64_t totalCycles(avgCp->totalCycles_);
      uint64_t avgCycles((uint64_t) (avgCp->totalCycles_/avgCp->iterations_));

      if(avgCycles > 999999 && totalCycles > 9999999)
      {
        totalCycles /= 1000000000;
        avgCycles   /= 1000000000;
        unitPtr = Checkpoint::SECOND_STR.c_str();
      }
      else if(avgCycles > 9999 && totalCycles > 99999)
      {
   	    totalCycles /= 1000;
        avgCycles   /= 1000;
   	    unitPtr = Checkpoint::MICRO_SEC_STR.c_str();
      }

      cout << "Weighted Average: Checkpoint [" << i
           << "] Iterations [" << avgCp->iterations_
           << "] Time [Unit,Avg,Total] = [" << unitPtr
           << ", " << avgCycles
           << ", " << totalCycles << "]\n";
    }
  }

  // Now print the threadId map
  cout << "\nTreadId map[" << threadIdCounter_ << "]" << endl;
  Checkpoint::ThreadIdMapType::iterator iter;
  for(iter = threadIdMap_.begin(); iter != threadIdMap_.end(); ++iter)
  {
    cout << "\t treadId [" << iter->first << "] => index [" << iter->second << "]\n";
  }
  cout << endl;

  if(useLocking_)
  {
    pthread_mutex_unlock(&checkpointLock_);
  }
}
