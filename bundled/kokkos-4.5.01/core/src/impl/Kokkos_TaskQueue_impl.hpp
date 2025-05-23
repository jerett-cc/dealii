//@HEADER
// ************************************************************************
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Part of Kokkos, under the Apache License v2.0 with LLVM Exceptions.
// See https://kokkos.org/LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//@HEADER

#ifndef KOKKOS_IMPL_TASKQUEUE_IMPL_HPP
#define KOKKOS_IMPL_TASKQUEUE_IMPL_HPP
#include <Kokkos_Macros.hpp>

#ifndef KOKKOS_ENABLE_DEPRECATED_CODE_4
#error "The tasking framework is deprecated"
#endif

#if defined(KOKKOS_ENABLE_TASKDAG)

#define KOKKOS_IMPL_DEBUG_TASKDAG_SCHEDULING 0

namespace Kokkos {
namespace Impl {

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
void TaskQueue<ExecSpace, MemorySpace>::Destroy::destroy_shared_allocation() {
  m_queue->~TaskQueue();
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
TaskQueue<ExecSpace, MemorySpace>::TaskQueue(
    typename TaskQueue<ExecSpace, MemorySpace>::memory_pool const
        &arg_memory_pool)
    : m_memory(arg_memory_pool),
      m_ready()
      //, m_accum_alloc(0)
      //, m_count_alloc(0)
      //, m_max_alloc(0)
      ,
      m_ready_count(0) {
  for (int i = 0; i < NumQueue; ++i) {
    m_ready[i][0] = (task_root_type *)task_root_type::EndTag;
    m_ready[i][1] = (task_root_type *)task_root_type::EndTag;
  }
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
TaskQueue<ExecSpace, MemorySpace>::~TaskQueue() {
  // Verify that queues are empty and ready count is zero

  for (int i = 0; i < NumQueue; ++i) {
    for (int j = 0; j < 2; ++j) {
      if (m_ready[i][j] != (task_root_type *)task_root_type::EndTag) {
        Kokkos::abort("TaskQueue::~TaskQueue ERROR: has ready tasks");
      }
    }
  }

  if (0 != m_ready_count) {
    Kokkos::abort("TaskQueue::~TaskQueue ERROR: has ready or executing tasks");
  }
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION void TaskQueue<ExecSpace, MemorySpace>::decrement(
    TaskQueue<ExecSpace, MemorySpace>::task_root_type *task) {
  task_root_type volatile &t = *task;

  const int count = Kokkos::atomic_fetch_add(&(t.m_ref_count), -1);
  Kokkos::memory_fence();

#if KOKKOS_IMPL_DEBUG_TASKDAG_SCHEDULING
  if (1 == count) {
    printf("decrement-destroy( 0x%lx { 0x%lx %d %d } )\n", uintptr_t(task),
           uintptr_t(task->m_next), int(task->m_task_type),
           int(task->m_ref_count));
  }
#endif

  if ((1 == count) && (t.m_next == (task_root_type *)task_root_type::LockTag)) {
    // Reference count is zero and task is complete, deallocate.

    // TaskQueue< ExecSpace, MemorySpace> * const queue =
    //  static_cast<scheduler_type const *>( t.m_scheduler )->m_queue;
    auto *const volatile queue = static_cast<TaskQueue *>(t.m_queue);

    // TODO @tasking @minor DSH this should call the destructor for a
    // non-trivially destructible type (possibly just ignore this in the old
    // version, though?) (Can't just do this; it needs to be queued since it's
    // device code if(task->m_destroy) task->m_destroy(task);

    queue->deallocate(task, t.m_alloc_size);
  } else if (count <= 1) {
    Kokkos::abort(
        "TaskScheduler task has negative reference count or is incomplete");
  }
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION size_t
TaskQueue<ExecSpace, MemorySpace>::allocate_block_size(size_t n) {
  return m_memory.allocate_block_size(n);
}

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION void *TaskQueue<ExecSpace, MemorySpace>::allocate(size_t n) {
  void *const p = m_memory.allocate(n);

  if (p) {
    desul::atomic_inc(
        &m_count_alloc, desul::MemoryOrderSeqCst(),
        desul::MemoryScopeDevice());  // TODO? memory_order_relaxed

    // if ( m_max_alloc < m_count_alloc ) m_max_alloc = m_count_alloc ;
  }

  return p;
}

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION void TaskQueue<ExecSpace, MemorySpace>::deallocate(void *p,
                                                                   size_t n) {
  m_memory.deallocate(p, n);
  desul::atomic_dec(&m_count_alloc, desul::MemoryOrderSeqCst(),
                    desul::MemoryScopeDevice());  // TODO? memory_order_relaxed
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION bool TaskQueue<ExecSpace, MemorySpace>::push_task(
    TaskQueue<ExecSpace, MemorySpace>::task_root_type *volatile *const queue,
    TaskQueue<ExecSpace, MemorySpace>::task_root_type *const task) {
  // Push task into a concurrently pushed and popped queue.
  // The queue can be either a ready task queue or a waiting task queue.
  // The queue is a linked list where 'task->m_next' form the links.
  // Fail the push attempt if the queue is locked;
  // otherwise retry until the push succeeds.

#if KOKKOS_IMPL_DEBUG_TASKDAG_SCHEDULING
  printf("push_task( 0x%lx { 0x%lx } 0x%lx { 0x%lx 0x%lx %d %d %d } )\n",
         uintptr_t(queue), uintptr_t(*queue), uintptr_t(task),
         uintptr_t(task->m_wait), uintptr_t(task->m_next), task->m_task_type,
         task->m_priority, task->m_ref_count);
#endif

  task_root_type *const zero = nullptr;
  task_root_type *const lock = (task_root_type *)task_root_type::LockTag;

  task_root_type *volatile &next = task->m_next;

  if (zero != next) {
    Kokkos::abort(
        "TaskQueue::push_task ERROR: already a member of another queue");
  }

  // store the head of the queue
  task_root_type *old_head = *queue;

  while (old_head != lock) {
    // set task->next to the head of the queue
    next = old_head;

    // Do not proceed until 'next' has been stored.
    Kokkos::memory_fence();

    // store the old head
    task_root_type *const old_head_tmp = old_head;

    // attempt to swap task with the old head of the queue
    // as if this were done atomically:
    //   if(*queue == old_head) {
    //     *queue = task;
    //   }
    //   old_head = *queue;
    old_head = desul::atomic_compare_exchange(
        const_cast<task_root_type **>(queue), old_head, task,
        desul::MemoryOrderSeqCst(), desul::MemoryScopeDevice());

    if (old_head_tmp == old_head) return true;
  }

  // Failed, replace 'task->m_next' value since 'task' remains
  // not a member of a queue.

  next = zero;

  // Do not proceed until 'next' has been stored.
  Kokkos::memory_fence();

  return false;
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION typename TaskQueue<ExecSpace, MemorySpace>::task_root_type *
TaskQueue<ExecSpace, MemorySpace>::pop_ready_task(
    TaskQueue<ExecSpace, MemorySpace>::task_root_type *volatile *const queue) {
  // Pop task from a concurrently pushed and popped ready task queue.
  // The queue is a linked list where 'task->m_next' form the links.

  task_root_type *const lock = (task_root_type *)task_root_type::LockTag;
  task_root_type *const end  = (task_root_type *)task_root_type::EndTag;

  // *queue is
  //   end   => an empty queue
  //   lock  => a locked queue
  //   valid

  // Retry until the lock is acquired or the queue is empty.

  task_root_type *task = *queue;

  while (end != task) {
    // The only possible values for the queue are
    // (1) lock, (2) end, or (3) a valid task.
    // Thus zero will never appear in the queue.
    //
    // If queue is locked then just read by guaranteeing the CAS will fail.

    if (lock == task) task = nullptr;

    task_root_type *const x = task;

    //    task = Kokkos::atomic_compare_exchange(queue, x, lock);
    task = desul::atomic_compare_exchange(const_cast<task_root_type **>(queue),
                                          x, lock, desul::MemoryOrderSeqCst(),
                                          desul::MemoryScopeDevice());

    if (x == task) {
      // CAS succeeded and queue is locked
      //
      // This thread has locked the queue and removed 'task' from the queue.
      // Extract the next entry of the queue from 'task->m_next'
      // and mark 'task' as popped from a queue by setting
      // 'task->m_next = lock'.
      //
      // Place the next entry in the head of the queue,
      // which also unlocks the queue.
      //
      // This thread has exclusive access to
      // the queue and the popped task's m_next.

      Kokkos::memory_fence();

      task_root_type *volatile &next = task->m_next;

      // This algorithm is not lockfree because a adversarial scheduler could
      // context switch this thread at this point and the rest of the threads
      // calling this method would never make forward progress

      *queue = next;
      next   = lock;

      Kokkos::memory_fence();

#if KOKKOS_IMPL_DEBUG_TASKDAG_SCHEDULING
      printf("pop_ready_task( 0x%lx 0x%lx { 0x%lx 0x%lx %d %d %d } )\n",
             uintptr_t(queue), uintptr_t(task), uintptr_t(task->m_wait),
             uintptr_t(task->m_next), int(task->m_task_type),
             int(task->m_priority), int(task->m_ref_count));
#endif

      return task;
    }
  }

  return end;
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION void TaskQueue<ExecSpace, MemorySpace>::schedule_runnable(
    TaskQueue<ExecSpace, MemorySpace>::task_root_type *const task) {
  // Schedule a runnable task upon construction / spawn
  // and upon completion of other tasks that 'task' is waiting on.
  //
  // Precondition:
  // - called by a single thread for the input task
  // - calling thread has exclusive access to the task
  // - task is not a member of a queue
  // - if runnable then task is either constructing or respawning
  //
  //   Constructing state:
  //     task->m_wait == 0
  //     task->m_next == dependence or 0
  //   Respawn state:
  //     task->m_wait == head of linked list: 'end' or valid task
  //     task->m_next == dependence or 0
  //
  //  Task state transition:
  //     Constructing ->  Waiting
  //     Respawn      ->  Waiting
  //
  //  Postcondition on task state:
  //     task->m_wait == head of linked list (queue)
  //     task->m_next == member of linked list (queue)

#if KOKKOS_IMPL_DEBUG_TASKDAG_SCHEDULING
  printf("schedule_runnable( 0x%lx { 0x%lx 0x%lx %d %d %d }\n", uintptr_t(task),
         uintptr_t(task->m_wait), uintptr_t(task->m_next), task->m_task_type,
         task->m_priority, task->m_ref_count);
#endif

  task_root_type *const zero = nullptr;
  task_root_type *const lock = (task_root_type *)task_root_type::LockTag;
  task_root_type *const end  = (task_root_type *)task_root_type::EndTag;

  task_root_type volatile &t = *task;

  bool respawn = false;

  //----------------------------------------

  if (zero == t.m_wait) {
    // Task in Constructing state
    // - Transition to Waiting state
    // Preconditions:
    // - call occurs exclusively within a single thread

    t.m_wait = end;
    // Task in Waiting state
  } else if (lock != t.m_wait) {
    // Task in Executing state with Respawn request
    // - Update dependence
    // - Transition to Waiting state
    respawn = true;
  } else {
    // Task in Complete state
    Kokkos::abort("TaskQueue::schedule_runnable ERROR: task is complete");
  }

  //----------------------------------------
  // Scheduling a runnable task which may have a depencency 'dep'.
  // Extract dependence, if any, from task->m_next.
  // If 'dep' is not null then attempt to push 'task'
  // into the wait queue of 'dep'.
  // If the push succeeds then 'task' may be
  // processed or executed by another thread at any time.
  // If the push fails then 'dep' is complete and 'task'
  // is ready to execute.

  // Exclusive access so don't need an atomic exchange
  // task_root_type * dep = Kokkos::atomic_exchange( & task->m_next , zero );
  task_root_type *dep = t.m_next;
  t.m_next            = zero;

  Kokkos::memory_fence();

  // If we don't have a dependency, or if pushing onto the wait queue of that
  // dependency failed (since the only time that queue should be locked is when
  // the task is transitioning to complete?)
  const bool is_ready = (nullptr == dep) || (!push_task(&dep->m_wait, task));

  if ((nullptr != dep) && respawn) {
    // Reference count for dep was incremented when
    // respawn assigned dependency to task->m_next
    // so that if dep completed prior to the
    // above push_task dep would not be destroyed.
    // dep reference count can now be decremented,
    // which may deallocate the task.
    TaskQueue::assign(&dep, nullptr);
  }

  if (is_ready) {
    // No dependence or 'dep' is complete so push task into ready queue.
    // Increment the ready count before pushing into ready queue
    // to track number of ready + executing tasks.
    // The ready count will be decremented when the task is complete.

    desul::atomic_inc(
        &m_ready_count, desul::MemoryOrderSeqCst(),
        desul::MemoryScopeDevice());  // TODO? memory_order_relaxed

    task_root_type *volatile *const ready_queue =
        &m_ready[t.m_priority][t.m_task_type];

    // A push_task fails if the ready queue is locked.
    // A ready queue is only locked during a push or pop;
    // i.e., it is never permanently locked.
    // Retry push to ready queue until it succeeds.
    // When the push succeeds then 'task' may be
    // processed or executed by another thread at any time.

    while (!push_task(ready_queue, task))
      ;
  }

  //----------------------------------------
  // Postcondition:
  // - A runnable 'task' was pushed into a wait or ready queue.
  // - Concurrent execution may have already popped 'task'
  //   from a queue and processed it as appropriate.
}

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION void TaskQueue<ExecSpace, MemorySpace>::schedule_aggregate(
    TaskQueue<ExecSpace, MemorySpace>::task_root_type *const task) {
  // Schedule an aggregate task upon construction
  // and upon completion of other tasks that 'task' is waiting on.
  //
  // Precondition:
  // - called by a single thread for the input task
  // - calling thread has exclusive access to the task
  // - task is not a member of a queue
  //
  //   Constructing state:
  //     task->m_wait == 0
  //     task->m_next == dependence or 0
  //
  //  Task state transition:
  //     Constructing ->  Waiting
  //
  //  Postcondition on task state:
  //     task->m_wait == head of linked list (queue)
  //     task->m_next == member of linked list (queue)

#if KOKKOS_IMPL_DEBUG_TASKDAG_SCHEDULING
  printf("schedule_aggregate( 0x%lx { 0x%lx 0x%lx %d %d %d %d }\n",
         uintptr_t(task), uintptr_t(task->m_wait), uintptr_t(task->m_next),
         task->m_dep_count, task->m_task_type, task->m_priority,
         task->m_ref_count);
#endif

  task_root_type *const zero = nullptr;
  task_root_type *const lock = (task_root_type *)task_root_type::LockTag;
  task_root_type *const end  = (task_root_type *)task_root_type::EndTag;

  task_root_type volatile &t = *task;

  //----------------------------------------

  if (zero == t.m_wait) {
    // Task in Constructing state
    // - Transition to Waiting state
    // Preconditions:
    // - call occurs exclusively within a single thread

    t.m_wait = end;
    // Task in Waiting state
  } else if (lock == t.m_wait) {
    // Task in Complete state
    Kokkos::abort("TaskQueue::schedule_aggregate ERROR: task is complete");
  }

  //----------------------------------------
  // Scheduling a 'when_all' task with multiple dependences.
  // This scheduling may be called when the 'when_all' is
  // (1) created or
  // (2) being removed from a completed task's wait list.

  task_root_type *volatile *const aggr = t.aggregate_dependences();

  // Assume the 'when_all' is complete until a dependence is
  // found that is not complete.

  bool is_complete = true;

  for (int i = t.m_dep_count; 0 < i && is_complete;) {
    --i;

    // Loop dependences looking for an incomplete task.
    // Add this task to the incomplete task's wait queue.

    // Remove a task 'x' from the dependence list.
    // The reference count of 'x' was incremented when
    // it was assigned into the dependence list.

    // Exclusive access so don't need an atomic exchange
    // task_root_type * x = Kokkos::atomic_exchange( aggr + i , zero );
    task_root_type *x = aggr[i];
    aggr[i]           = zero;

    if (x) {
      // If x->m_wait is not locked then push succeeds
      // and the aggregate is not complete.
      // If the push succeeds then this when_all 'task' may be
      // processed by another thread at any time.
      // For example, 'x' may be completeed by another
      // thread and then re-schedule this when_all 'task'.

      is_complete = !push_task(&x->m_wait, task);

      // Decrement reference count which had been incremented
      // when 'x' was added to the dependence list.

      TaskQueue::assign(&x, zero);
    }
  }

  if (is_complete) {
    // The when_all 'task' was not added to a wait queue because
    // all dependences were complete so this aggregate is complete.
    // Complete the when_all 'task' to schedule other tasks
    // that are waiting for the when_all 'task' to complete.

    t.m_next = lock;

    complete(task);

    // '*task' may have been deleted upon completion
  }

  //----------------------------------------
  // Postcondition:
  // - An aggregate 'task' was either pushed to a wait queue or completed.
  // - Concurrent execution may have already popped 'task'
  //   from a queue and processed it as appropriate.
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION void TaskQueue<ExecSpace, MemorySpace>::reschedule(
    task_root_type *task) {
  // Precondition:
  //   task is in Executing state
  //   task->m_next == LockTag
  //
  // Postcondition:
  //   task is in Executing-Respawn state
  //   task->m_next == 0 (no dependence)

  task_root_type *const zero = nullptr;
  task_root_type *const lock = (task_root_type *)task_root_type::LockTag;
  if (lock != desul::atomic_exchange(&task->m_next, zero,
                                     desul::MemoryOrderSeqCst(),
                                     desul::MemoryScopeDevice())) {
    Kokkos::abort("TaskScheduler::respawn ERROR: already respawned");
  }
}

//----------------------------------------------------------------------------

template <typename ExecSpace, typename MemorySpace>
KOKKOS_FUNCTION void TaskQueue<ExecSpace, MemorySpace>::complete(
    TaskQueue<ExecSpace, MemorySpace>::task_root_type *task) {
  // Complete a runnable task that has finished executing
  // or a when_all task when all of its dependeneces are complete.

  task_root_type *const zero = nullptr;
  task_root_type *const lock = (task_root_type *)task_root_type::LockTag;
  task_root_type *const end  = (task_root_type *)task_root_type::EndTag;

#if KOKKOS_IMPL_DEBUG_TASKDAG_SCHEDULING
  printf("complete( 0x%lx { 0x%lx 0x%lx %d %d %d }\n", uintptr_t(task),
         uintptr_t(task->m_wait), uintptr_t(task->m_next), task->m_task_type,
         task->m_priority, task->m_ref_count);
#endif

  task_root_type volatile &t = *task;

  const bool runnable = task_root_type::Aggregate != t.m_task_type;

  //----------------------------------------

  if (runnable && lock != t.m_next) {
    // Is a runnable task has finished executing and requested respawn.
    // Schedule the task for subsequent execution.

    schedule_runnable(task);
  }
  //----------------------------------------
  else {
    // Is either an aggregate or a runnable task that executed
    // and did not respawn.  Transition this task to complete.

    // If 'task' is an aggregate then any of the runnable tasks that
    // it depends upon may be attempting to complete this 'task'.
    // Must only transition a task once to complete status.
    // This is controlled by atomically locking the wait queue.

    // Stop other tasks from adding themselves to this task's wait queue
    // by locking the head of this task's wait queue.
    task_root_type *x = desul::atomic_exchange(
        const_cast<task_root_type **>(&t.m_wait), lock,
        desul::MemoryOrderSeqCst(), desul::MemoryScopeDevice());

    if (x != (task_root_type *)lock) {
      // This thread has transitioned this 'task' to complete.
      // 'task' is no longer in a queue and is not executing
      // so decrement the reference count from 'task's creation.
      // If no other references to this 'task' then it will be deleted.

      TaskQueue::assign(&task, zero);

      // This thread has exclusive access to the wait list so
      // the concurrency-safe pop_ready_task function is not needed.
      // Schedule the tasks that have been waiting on the input 'task',
      // which may have been deleted.

      while (x != end) {
        // Have exclusive access to 'x' until it is scheduled
        // Set x->m_next = zero  <=  no dependence, not a respawn

        task_root_type volatile &vx = *x;

        task_root_type *const next = vx.m_next;
        vx.m_next                  = nullptr;

        Kokkos::memory_fence();

        if (task_root_type::Aggregate != vx.m_task_type) {
          schedule_runnable(x);
        } else {
#if !defined(__HIP_DEVICE_COMPILE__)
          schedule_aggregate(x);
#endif
        }

        x = next;
      }
    }
  }

  if (runnable) {
    // A runnable task was popped from a ready queue and executed.
    // If respawned into a ready queue then the ready count was incremented
    // so decrement whether respawned or not.
    desul::atomic_dec(
        &m_ready_count, desul::MemoryOrderSeqCst(),
        desul::MemoryScopeDevice());  // TODO? memory_order_relaxed
  }
}

//----------------------------------------------------------------------------

} /* namespace Impl */
} /* namespace Kokkos */

#endif /* #if defined( KOKKOS_ENABLE_TASKDAG ) */
#endif /* #ifndef KOKKOS_IMPL_TASKQUEUE_IMPL_HPP */
