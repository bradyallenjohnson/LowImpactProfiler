
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
// This method is not thread-safe, so it must be called before the threads are started
void Checkpoint::initialize(uint32_t numThreads, bool useLocking) /* default values: DEFAULT_MAX_THREADS, true */
{
  Checkpoint::useLocking_ = useLocking;
  if(instance_ == 0)
  {
    instance_ = new Checkpoint(numThreads);
  }
}

// static
// Singleton method to create/retrieve Checkpoint object
// If called before initializing, then initialize() will be called
Checkpoint* Checkpoint::instance()
{
  if(instance_ == 0)
  {
    Checkpoint::initialize();
  }

  return instance_;
}

// static
// This method is not thread-safe, so it must only be called when all the threads are finished
void Checkpoint::destroy()
{
  if(instance_ == 0)
  {
    delete instance_;
    instance_ = 0;
  }
}

// protected
Checkpoint::Checkpoint(uint32_t numThreads) :
    numThreads_(numThreads),
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
  pthread_rwlock_init(&threadCpInfoMapRwLock_, NULL);

  threadIdVector_.reserve(numThreads);
}

// private
Checkpoint::ThreadCheckpointInfo *Checkpoint::getThreadCpInfo()
{
  pthread_t threadId(pthread_self());
  ThreadCheckpointInfo *threadCpInfo(NULL);

  pthread_rwlock_rdlock(&threadCpInfoMapRwLock_);
  Checkpoint::ThreadCpInfoMapType::iterator iter(threadCpInfoMap_.find(threadId));

  if(iter != threadCpInfoMap_.end())
  {
    threadCpInfo = &(iter->second);
    pthread_rwlock_unlock(&threadCpInfoMapRwLock_);
  }
  else
  {
    // release the read lock
    pthread_rwlock_unlock(&threadCpInfoMapRwLock_);

    // get a write lock, since we'll have to modify the map
    pthread_rwlock_wrlock(&threadCpInfoMapRwLock_);
      threadIdVector_[threadIdCounter_++] = threadId;
      threadCpInfoMap_[threadId] = ThreadCheckpointInfo();
      threadCpInfo = &(threadCpInfoMap_[threadId]);
    pthread_rwlock_unlock(&threadCpInfoMapRwLock_);
  }

  return threadCpInfo;
}


// Method to calculate current checkpoint information
void Checkpoint::checkpoint(int checkpoint)
{
  if(__unlikely(!isActive_)) {
    return;
  }

  // Not checking threadNum nor checkpoint for performance reasons

  ThreadCheckpointInfo *threadCp (  getThreadCpInfo() );
  CheckpointInfo *currentCp      (  &(threadCp->checkpoints_[checkpoint]) );
  CheckpointInfo *previousCp     (  &(threadCp->checkpoints_[threadCp->lastCheckpointHit_]) );
  threadCp->lastCheckpointHit_  =  checkpoint;

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

const char *Checkpoint::getTimeResolutionStr(uint64_t &avgCycles, uint64_t &totalCycles)
{
  const char *unitPtr(Checkpoint::NANO_SEC_STR.c_str());

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

  return unitPtr;
}

// Dump all the checkpoint information
void Checkpoint::dump()
{
  cout << "Number of Threads [configured, used] = [" << numThreads_
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

  CheckpointInfo totalCpAvg[MAX_CHECKPOINT];
  uint32_t numCpHits[MAX_CHECKPOINT];
  memset(numCpHits, 0, sizeof(uint32_t)*MAX_CHECKPOINT);

  // Iterate over the vector and index the map, because if we iterate the map
  // the key is the threadId and the threads will be ordered by the threadId
  for(int thread = 0; thread < threadIdCounter_; ++thread)
  {
    ThreadCheckpointInfo *threadCp = &(threadCpInfoMap_[threadIdVector_[thread]]);

    // Filter out unused threads
    bool threadUsed(false);
    CheckpointInfo *currentCp(threadCp->checkpoints_);
    for(int chkPoint=0; chkPoint < MAX_CHECKPOINT; currentCp = &(threadCp->checkpoints_[++chkPoint]))
    {
      if(currentCp->iterations_ != 0)
      {
        threadUsed = true;
      }
    }

    if(!threadUsed)
    {
      //cout << "Thread [" << thread << "] No checkpoints hit on this thread, skipping\n" << endl;
      continue;
    }

    // Filter out unused checkpoints
    int maxCpIndex(MAX_CHECKPOINT);
    currentCp = &(threadCp->checkpoints_[MAX_CHECKPOINT-1]);
    for(int chkPoint=MAX_CHECKPOINT; chkPoint > 0; currentCp = &(threadCp->checkpoints_[--chkPoint]))
    {
      if(currentCp->iterations_ == 0)
      {
        maxCpIndex = chkPoint;
      }
      else
      {
        break;
      }
    }

    currentCp = threadCp->checkpoints_;
    for(int chkPoint=0; chkPoint < maxCpIndex; currentCp = &(threadCp->checkpoints_[++chkPoint]))
    {
      uint64_t avgCycles(0);

      if(currentCp->iterations_ != 0)
      {
        // Avoiding possible divide by zero
        avgCycles = currentCp->totalCycles_/currentCp->iterations_;
        totalCpAvg[chkPoint] += currentCp;
        numCpHits[chkPoint]++;
      }

      uint64_t totalCycles(currentCp->totalCycles_);
      const char *unitPtr(getTimeResolutionStr(avgCycles, totalCycles));

      cout << "Thread [" << thread
           << "] Checkpoint [" << chkPoint
           << "] Iterations [" << currentCp->iterations_
           << "] Time [Unit,Avg,Total] = [" << unitPtr
           << ", " << avgCycles
           << ", " << totalCycles << "]\n";
    }
    cout << endl;
  }

  // Now print the averages
  for(int i = 0; i < MAX_CHECKPOINT; ++i)
  {
    CheckpointInfo *avgCp = &(totalCpAvg[i]);
    if(numCpHits[i] > 1)
    {
      avgCp->totalCycles_ = avgCp->totalCycles_/numCpHits[i];
      avgCp->iterations_ = avgCp->iterations_/numCpHits[i];

      uint64_t totalCycles(avgCp->totalCycles_);
      uint64_t avgCycles((uint64_t) (avgCp->totalCycles_/avgCp->iterations_));
      const char *unitPtr(getTimeResolutionStr(avgCycles, totalCycles));

      cout << "Weighted Average: Checkpoint [" << i
           << "] Iterations [" << avgCp->iterations_
           << "] Time [Unit,Avg,Total] = [" << unitPtr
           << ", " << avgCycles
           << ", " << totalCycles << "]\n";
    }
  }

  // Now print the threadId map
  cout << "\nTreadIds [" << threadIdCounter_ << "]" << endl;
  for(int i = 0; i < threadIdCounter_; ++i)
  {
    cout << "\t thread [" << i << "] => threadId [" << threadIdVector_[i] << "]\n";
  }
  cout << endl;

  if(useLocking_)
  {
    pthread_mutex_unlock(&checkpointLock_);
  }
}
