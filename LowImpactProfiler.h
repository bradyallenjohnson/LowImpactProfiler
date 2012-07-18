#ifndef LOW_IMPACT_PROFILER_H
#define LOW_IMPACT_PROFILER_H

#define CHECKPOINT(cpNum) Checkpoint::instance()->checkpoint(cpNum)
#define __unlikely(condition) __builtin_expect(!!(condition), 0)
#define __likely(condition)   __builtin_expect(!!(condition), 1)

#include <map>
#include <string>

using namespace std;

class Checkpoint
{
  public:
    static const int MAX_CHK=10;
    static const int MAX_CORES=32;
    static const string SECOND_STR;
    static const string MICRO_SEC_STR;
    static const string NANO_SEC_STR;

    // Allow Checkpoints to not start gathering until ordered to do so
    inline void setActive(bool active) { isActive_ = active; }

    void setNumCores(int numCores);

    // The Checkpoint object is a singleton, this method obtains the instance
    static Checkpoint* instance();

    // Both instance() and initialize() will initialize the checkpoints
    // but Init provides more flexibility
    // - If not interested in HW registers, set to false to make a checkpoint more performant
    // - locking is only necessary if dump() will be called while checkpoints are being taken
    //   If dump() will only be called at the end, then set false for increased performance
    static void initialize(uint32_t numCores = MAX_CORES, bool useLocking = true);

    // Gather checkpoint info for the specified checkpoint
    void checkpoint(int checkpoint);

    // Dump the checkpoint info gathered to stdout
    void dump();

  private:
    uint32_t getCoreNum();

    // returns nanoseconds since the epoch
    // TODO make sure putting both the seconds and ns in a uint64_t doesnt overflow it
    inline uint64_t getCycles() {
      struct timespec now;
      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
      return (now.tv_sec * 1000000000) + now.tv_nsec;
    }

    struct chkInfo{
      uint64_t iterations_;
      uint64_t totalCycles_;
      uint64_t previousCycles_;
      chkInfo() : iterations_(0), totalCycles_(0), previousCycles_(0)
      {}
      chkInfo *operator+=(chkInfo *cpRhs)
      {
        if(this == cpRhs) {return this;}
        this->iterations_        +=  cpRhs->iterations_;
        this->totalCycles_       +=  cpRhs->totalCycles_;
        this->previousCycles_    +=  cpRhs->previousCycles_;
        return this;
      }
    };

    struct coreInfo {
      struct chkInfo checkpoints_[MAX_CHK];
      uint32_t lastCheckpointHit_;
    };

    static Checkpoint* instance_;
    pthread_mutex_t checkpointLock_;
    struct coreInfo lipInfo_[MAX_CORES];
    int numCores_;
    bool isActive_;

    typedef map<pthread_t, uint32_t> ThreadIdMapType;
    ThreadIdMapType threadIdMap_;
    pthread_rwlock_t threadIdMapRwLock_;
    uint32_t threadIdCounter_;

    static bool useLocking_;

  protected:
    Checkpoint(uint32_t numCores);
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
