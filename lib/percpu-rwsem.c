#include <linux/percpu-rwsem.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>

int percpu_init_rwsem(struct percpu_rw_semaphore *brw)
{
	brw->fast_read_ctr = alloc_percpu(int);
	if (unlikely(!brw->fast_read_ctr))
		return -ENOMEM;

	mutex_init(&brw->writer_mutex);
	init_rwsem(&brw->rw_sem);
	atomic_set(&brw->slow_read_ctr, 0);
	init_waitqueue_head(&brw->write_waitq);
	return 0;
}

void percpu_free_rwsem(struct percpu_rw_semaphore *brw)
{
	free_percpu(brw->fast_read_ctr);
	brw->fast_read_ctr = NULL; /* catch use after free bugs */
}

static bool update_fast_ctr(struct percpu_rw_semaphore *brw, unsigned int val)
{
	bool success = false;

	preempt_disable();
	if (likely(!mutex_is_locked(&brw->writer_mutex))) {
		__this_cpu_add(*brw->fast_read_ctr, val);
		success = true;
	}
	preempt_enable();

	return success;
}

/*
 * Like the normal down_read() this is not recursive, the writer can
 * come after the first percpu_down_read() and create the deadlock.
 */
void percpu_down_read(struct percpu_rw_semaphore *brw)
{
	if (likely(update_fast_ctr(brw, +1)))
		return;

	down_read(&brw->rw_sem);
	atomic_inc(&brw->slow_read_ctr);
	up_read(&brw->rw_sem);
}

void percpu_up_read(struct percpu_rw_semaphore *brw)
{
	if (likely(update_fast_ctr(brw, -1)))
		return;

	/* false-positive is possible but harmless */
	if (atomic_dec_and_test(&brw->slow_read_ctr))
		wake_up_all(&brw->write_waitq);
}

static int clear_fast_ctr(struct percpu_rw_semaphore *brw)
{
	unsigned int sum = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		sum += per_cpu(*brw->fast_read_ctr, cpu);
		per_cpu(*brw->fast_read_ctr, cpu) = 0;
	}

	return sum;
}

/*
 * A writer takes ->writer_mutex to exclude other writers and to force the
 * readers to switch to the slow mode, note the mutex_is_locked() check in
 * update_fast_ctr().
 *
 * After that the readers can only inc/dec the slow ->slow_read_ctr counter,
 * ->fast_read_ctr is stable. Once the writer moves its sum into the slow
 * counter it represents the number of active readers.
 *
 * Finally the writer takes ->rw_sem for writing and blocks the new readers,
 * then waits until the slow counter becomes zero.
 */
void percpu_down_write(struct percpu_rw_semaphore *brw)
{
	/* also blocks update_fast_ctr() which checks mutex_is_locked() */
	mutex_lock(&brw->writer_mutex);

	/*
	 * 1. Ensures mutex_is_locked() is visible to any down_read/up_read
	 *    so that update_fast_ctr() can't succeed.
	 *
	 * 2. Ensures we see the result of every previous this_cpu_add() in
	 *    update_fast_ctr().
	 *
	 * 3. Ensures that if any reader has exited its critical section via
	 *    fast-path, it executes a full memory barrier before we return.
	 */
	synchronize_sched_expedited();

	/* nobody can use fast_read_ctr, move its sum into slow_read_ctr */
	atomic_add(clear_fast_ctr(brw), &brw->slow_read_ctr);

	/* block the new readers completely */
	down_write(&brw->rw_sem);

	/* wait for all readers to complete their percpu_up_read() */
	wait_event(brw->write_waitq, !atomic_read(&brw->slow_read_ctr));
}

void percpu_up_write(struct percpu_rw_semaphore *brw)
{
	/* allow the new readers, but only the slow-path */
	up_write(&brw->rw_sem);

	/* insert the barrier before the next fast-path in down_read */
	synchronize_sched_expedited();

	mutex_unlock(&brw->writer_mutex);
}
