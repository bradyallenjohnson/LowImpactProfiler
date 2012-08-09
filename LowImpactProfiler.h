#ifndef LOW_IMPACT_PROFILER_H
#define LOW_IMPACT_PROFILER_H

#include <map>
#include <string>
#include <vector>

#include <stdint.h> // uint32_t et al

#define CHECKPOINT(cpNum) Checkpoint::instance()->checkpoint(cpNum)
#define __unlikely(condition) __builtin_expect(!!(condition), 0)
#define __likely(condition)   __builtin_expect(!!(condition), 1)

using namespace std;

class Checkpoint
{
  public:
    static const int MAX_CHECKPOINT=10;
    static const int DEFAULT_MAX_THREADS=32;
    static const string SECOND_STR;
    static const string MICRO_SEC_STR;
    static const string NANO_SEC_STR;

    // Allow Checkpoints to not start gathering until ordered to do so
    inline void setActive(bool active) { isActive_ = active; }

    void setNumThreads(int numThreads);

    // The Checkpoint object is a singleton, this method obtains the instance
    static Checkpoint* instance();

    // Destroy the singleton instance
    statid void destroy();

    // Both instance() and initialize() will initialize the checkpoints
    // but Init provides more flexibility
    // - locking is only necessary if dump() will be called while checkpoints are being taken
    //   If dump() will only be called at the end, then set false for increased performance
    static void initialize(uint32_t numThreads = DEFAULT_MAX_THREADS, bool useLocking = true);

    // Gather checkpoint info for the specified checkpoint
    void checkpoint(int checkpoint);

    // Dump the checkpoint info gathered to stdout
    void dump();

  private:
    typedef struct CheckpointInfo_s {
      uint64_t iterations_;
      uint64_t totalCycles_;
      uint64_t previousCycles_;
      CheckpointInfo_s() : iterations_(0), totalCycles_(0), previousCycles_(getCycles()) {}
      CheckpointInfo_s *operator+=(CheckpointInfo_s *cpRhs) {
        if(this == cpRhs) {return this;}
        this->iterations_        +=  cpRhs->iterations_;
        this->totalCycles_       +=  cpRhs->totalCycles_;
        this->previousCycles_    +=  cpRhs->previousCycles_;
        return this;
      }
    } CheckpointInfo;

    typedef struct ThreadCheckpointInfo_s {
      CheckpointInfo checkpoints_[MAX_CHECKPOINT];
      uint32_t lastCheckpointHit_;
      ThreadCheckpointInfo_s() : lastCheckpointHit_(0) {}
    } ThreadCheckpointInfo;

    // internally gets the threadId and returns the corresponding ThreadCheckpointInfo
    // uses a rw lock for internal attribute protection
    ThreadCheckpointInfo *getThreadCpInfo();

    // returns nanoseconds since the epoch
    // TODO make sure putting both the seconds and ns in a uint64_t doesnt overflow it
    static inline uint64_t getCycles() {
      struct timespec now;
      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
      return (now.tv_sec * 1000000000) + now.tv_nsec;
    }

    // returns one of SECOND_STR, MICRO_SEC_STR, or NANO_SEC_STR
    static const char *getTimeResolutionStr(uint64_t &avgCycles, uint64_t &totalCycles);

    static Checkpoint* instance_;
    static bool useLocking_;

    // Map threadId to ThreadCheckpointInfo
    typedef map<pthread_t, ThreadCheckpointInfo> ThreadCpInfoMapType;
    ThreadCpInfoMapType threadCpInfoMap_;
    pthread_rwlock_t threadCpInfoMapRwLock_;

    typedef vector<pthread_t> ThreadIdVectorType;
    ThreadIdVectorType threadIdVector_;

    pthread_mutex_t checkpointLock_;
    uint32_t threadIdCounter_;
    int numThreads_;
    bool isActive_;

  protected:
    Checkpoint(uint32_t numThreads);
};

//
// ScopedCheckpoint
//
// To be used in functions or scopes, where you want a checkpoint at scope entry
// and via the ScopedCheckpoint destruction, another checkpoint at scope exit
// A ScopedCheckpoint object should be instantiated at scope entry with a checkpointNumber.
// Upon ScopedCheckpoint instantiation, a checkpoint will be taken using checkpointNumber.
// Then on scope exit, the object will be destroyed, and a checkpoint will be taken using
// either checkpointNumber+1 or if the 2 arg ctor was used, then lastCheckpoint

class ScopedCheckpoint
{
public:
  ScopedCheckpoint(int checkpoint) :
      startCheckpointNumber_(checkpoint),
      lastCheckpointNumber_(checkpoint+1)
  {
    Checkpoint::instance()->checkpoint(startCheckpointNumber_);
  }

  ScopedCheckpoint(int startCheckpoint, int lastCheckpoint) :
      startCheckpointNumber_(startCheckpoint),
      lastCheckpointNumber_(lastCheckpoint)
  {
    Checkpoint::instance()->checkpoint(startCheckpointNumber_);
  }

  ~ScopedCheckpoint()
  {
    Checkpoint::instance()->checkpoint(lastCheckpointNumber_);
  }

private:
  ScopedCheckpoint();
  int startCheckpointNumber_;
  int lastCheckpointNumber_;
};

#endif // LOW_IMPACT_PROFILER_H
