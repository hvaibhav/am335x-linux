/*
 * Only give sleepers 50% of their service deficit. This allows
 * them to run sooner, but does not allow tons of sleepers to
 * rip the spread apart.
 */
SCHED_FEAT(GENTLE_FAIR_SLEEPERS, true)

/*
 * Place new tasks ahead so that they do not starve already running
 * tasks
 */
SCHED_FEAT(START_DEBIT, true)

/*
 * Prefer to schedule the task we woke last (assuming it failed
 * wakeup-preemption), since its likely going to consume data we
 * touched, increases cache locality.
 */
SCHED_FEAT(NEXT_BUDDY, false)

/*
 * Prefer to schedule the task that ran last (when we did
 * wake-preempt) as that likely will touch the same data, increases
 * cache locality.
 */
SCHED_FEAT(LAST_BUDDY, true)

/*
 * Consider buddies to be cache hot, decreases the likelyness of a
 * cache buddy being migrated away, increases cache locality.
 */
SCHED_FEAT(CACHE_HOT_BUDDY, true)

/*
 * Allow wakeup-time preemption of the current task:
 */
SCHED_FEAT(WAKEUP_PREEMPTION, true)

/*
 * Use arch dependent cpu power functions
 */
SCHED_FEAT(ARCH_POWER, true)

SCHED_FEAT(HRTICK, false)
SCHED_FEAT(DOUBLE_TICK, false)
SCHED_FEAT(LB_BIAS, true)

/*
 * Spin-wait on mutex acquisition when the mutex owner is running on
 * another cpu -- assumes that when the owner is running, it will soon
 * release the lock. Decreases scheduling overhead.
 */
SCHED_FEAT(OWNER_SPIN, true)

/*
 * Decrement CPU power based on time not spent running tasks
 */
SCHED_FEAT(NONTASK_POWER, true)

/*
 * Queue remote wakeups on the target CPU and process them
 * using the scheduler IPI. Reduces rq->lock contention/bounces.
 */
SCHED_FEAT(TTWU_QUEUE, true)

SCHED_FEAT(NUMA_SETTLE,			true)

SCHED_FEAT(FORCE_SD_OVERLAP,		false)
SCHED_FEAT(RT_RUNTIME_SHARE,		true)
SCHED_FEAT(LB_MIN,			false)
SCHED_FEAT(IDEAL_CPU,			true)
SCHED_FEAT(IDEAL_CPU_THREAD_BIAS,	false)
SCHED_FEAT(PUSH_PRIVATE_BUDDIES,	true)
SCHED_FEAT(PUSH_SHARED_BUDDIES,		true)
SCHED_FEAT(WAKE_ON_IDEAL_CPU,		false)

#ifdef CONFIG_NUMA_BALANCING
/* Do the working set probing faults: */
SCHED_FEAT(NUMA,			true)
SCHED_FEAT(NUMA_BALANCE_ALL,		false)
SCHED_FEAT(NUMA_BALANCE_INTERNODE,	false)
SCHED_FEAT(NUMA_EXCLUDE_AFFINE,		true)
SCHED_FEAT(NUMA_LB,			false)
SCHED_FEAT(NUMA_GROUP_LB_COMPRESS,	true)
SCHED_FEAT(NUMA_GROUP_LB_SPREAD,	true)
SCHED_FEAT(MIGRATE_FAULT_STATS,		false)
SCHED_FEAT(NUMA_POLICY_ADAPTIVE,	false)
SCHED_FEAT(NUMA_POLICY_SYSWIDE,		false)
SCHED_FEAT(NUMA_POLICY_MAXNODE,		false)
SCHED_FEAT(NUMA_POLICY_MAXBUDDIES,	false)
SCHED_FEAT(NUMA_POLICY_MANYBUDDIES,	true)

SCHED_FEAT(NUMA_CONVERGE_MIGRATIONS,	true)
#endif
