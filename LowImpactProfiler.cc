
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
const string Checkpoint::MILLI_SEC_STR = "MilliSec";
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
// Destroys the Singleton instance
// This method is not thread-safe, so it must only be called when all the threads are finished
void Checkpoint::destroy()
{
  if(instance_ != 0)
  {
    delete instance_;
  }
  instance_ = 0;
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
  pthread_rwlockattr_init(&rwlockAttr_);
  // give priority to writers
  pthread_rwlockattr_setkind_np(&rwlockAttr_, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
  pthread_rwlock_init(&threadCpInfoMapRwLock_, &rwlockAttr_);

  if(numThreads > 1)
  {
    threadIdVector_.reserve(numThreads);
  }
  else
  {
    threadIdVector_.reserve(1);
  }
}

Checkpoint::~Checkpoint()
{
  pthread_rwlockattr_destroy(&rwlockAttr_);
  pthread_rwlock_destroy(&threadCpInfoMapRwLock_);
  pthread_mutex_destroy(&checkpointLock_);
}

// private
Checkpoint::ThreadCheckpointInfo *Checkpoint::getThreadCpInfo()
{
  ThreadCheckpointInfo *threadCpInfo(NULL);

  if(numThreads_ == 0)
  {
    if(__unlikely(threadCpInfoMap_.empty()))
    {
      threadCpInfoMap_[0] = ThreadCheckpointInfo();
      threadIdVector_[threadIdCounter_++] = 0;
    }
    threadCpInfo = &(threadCpInfoMap_[0]);

    return threadCpInfo;
  }

  pthread_t threadId(pthread_self());
  pthread_rwlock_rdlock(&threadCpInfoMapRwLock_);
  Checkpoint::ThreadCpInfoMapType::iterator iter(threadCpInfoMap_.find(threadId));

  if(__likely(iter != threadCpInfoMap_.end()))
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
  const char *unitPtr(Checkpoint::MICRO_SEC_STR.c_str());

  if(avgCycles > 99999LU && totalCycles > 999999LU)
  {
    totalCycles /= 1000000LU;
    avgCycles   /= 1000000LU;
    unitPtr = Checkpoint::SECOND_STR.c_str();
  }
  else if(avgCycles > 9999 && totalCycles > 99999)
  {
    totalCycles /= 1000;
    avgCycles   /= 1000;
    unitPtr = Checkpoint::MILLI_SEC_STR.c_str();
  }

  return unitPtr;
}

// Dump all the checkpoint information
void Checkpoint::dump(ostream &out, bool dumpAverages, bool dumpTput, bool dumpThreadIds)
{
  out << "Number of Threads [configured, used] = [" << numThreads_
       << ", " << threadIdCounter_
       << "]"
       << endl;

  struct timespec resolution;
  //clock_getres(CLOCK_THREAD_CPUTIME_ID, &resolution);
  clock_getres(CLOCK_REALTIME, &resolution);
  out << "Timer resolution in nanoseconds [" << resolution.tv_nsec << "]" << endl;

  if(useLocking_)
  {
    pthread_mutex_lock(&checkpointLock_);
  }

  CheckpointInfo totalCpAvg[MAX_CHECKPOINT];
  uint32_t numCpHits[MAX_CHECKPOINT];
  memset(numCpHits, 0, sizeof(uint32_t)*MAX_CHECKPOINT);

  // Print a summary of the Checkpoints for each Thread
  //
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
      //out << "Thread [" << thread << "] No checkpoints hit on this thread, skipping\n" << endl;
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
    for(int checkPoint=0; checkPoint < maxCpIndex; currentCp = &(threadCp->checkpoints_[++checkPoint]))
    {
      uint64_t avgCycles(0);

      if(currentCp->iterations_ != 0)
      {
        // Avoiding possible divide by zero
        avgCycles = currentCp->totalCycles_/currentCp->iterations_;
        totalCpAvg[checkPoint] += currentCp;
        numCpHits[checkPoint]++;
      }

      uint64_t totalCycles(currentCp->totalCycles_);
      const char *unitPtr(getTimeResolutionStr(avgCycles, totalCycles));

      out << "Thread [" << thread
           << "] Checkpoint [" << checkPoint
           << "] Iterations [" << currentCp->iterations_
           << "] Time [Unit,Avg,Total] = [" << unitPtr
           << ", " << avgCycles
           << ", " << totalCycles << "]\n";
    }
    out << endl;
  }

  // Now print the averages
  if(dumpAverages)
  {
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

        out << "Weighted Average: Checkpoint [" << i
             << "] Iterations [" << avgCp->iterations_
             << "] Time [Unit,Avg,Total] = [" << unitPtr
             << ", " << avgCycles
             << ", " << totalCycles << "]\n";
      }
    }
  }

  // Now print the approximated Throughput
  if(dumpTput)
  {
    dumpThroughput(out);
  }

  // Now print the threadId map
  if(dumpThreadIds)
  {
    out << "\nTreadIds [" << threadIdCounter_ << "]" << endl;
    for(int i = 0; i < threadIdCounter_; ++i)
    {
      out << "\t thread [" << i << "] => threadId [" << threadIdVector_[i] << "]\n";
    }
    out << endl;
  }

  if(useLocking_)
  {
    pthread_mutex_unlock(&checkpointLock_);
  }
}

void Checkpoint::dumpThroughput(ostream &out)
{
  // Thread 0 is the first thread created, so its creation time
  // will be the start time for the throughput calculation
  ThreadCheckpointInfo *threadZeroCps(&(threadCpInfoMap_[threadIdVector_[0]]));

  // Assuming all threads have hit the same checkpoints,
  // so lets get the last checkpoint hit from thread 0
  int maxCpIndex(MAX_CHECKPOINT);
  CheckpointInfo *currentCp = &(threadZeroCps->checkpoints_[MAX_CHECKPOINT-1]);
  for(int checkPoint = MAX_CHECKPOINT; checkPoint > 0; currentCp = &(threadZeroCps->checkpoints_[--checkPoint]))
  {
    if(currentCp->iterations_ != 0)
    {
      maxCpIndex = checkPoint;
      break;
    }
  }

  out << "\nThroughput for each thread cp[" << maxCpIndex << "]:\n";

  // Now get the greatest previousCycles from the last checkpoint hit to get the endTime
  // Iterate over the vector and index the map
  float totalThroughput(0);
  for(int thread = 0; thread < threadIdCounter_; ++thread)
  {
    ThreadCheckpointInfo *threadCp = &(threadCpInfoMap_[threadIdVector_[thread]]);
    uint64_t startTime(threadCp->creationCycles_);
    uint64_t endTime(threadCp->checkpoints_[maxCpIndex].previousCycles_);
    uint64_t iterations(threadCp->checkpoints_[maxCpIndex].iterations_);
    float throughput = (iterations/((float) (endTime - startTime)/1000000.0));
    totalThroughput += throughput;

    out << "Thread[" << thread << "] Time usec [start, end, diff] = [" << startTime
        << ", " << endTime
        << ", " << (endTime - startTime)
        << "], iterations = " << iterations
        << ", throughput (iters/sec) = " << throughput
        << endl;
  }

  out << "\nTotal Throughput (iters/sec) = " << totalThroughput << endl;
}
