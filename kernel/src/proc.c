#include "klib.h"
#include "cte.h"
#include "proc.h"

#define PROC_NUM 64

static __attribute__((used)) int next_pid = 1;

proc_t pcb[PROC_NUM];
static proc_t *curr = &pcb[0];

void init_proc()
{
  // Lab2-1, set status and pgdir
  proc_t *kproc = &pcb[0];
  kproc->status = RUNNING;
  kproc->pgdir = vm_curr();
  kproc->kstack = (void *)(KER_MEM - PGSIZE);
  // Lab2-4, init zombie_sem
  sem_init(&kproc->zombie_sem, 0);
  // Lab3-2, set cwd
  kproc->cwd = iopen("/", TYPE_NONE);
}

proc_t *proc_alloc()
{
  // Lab2-1: find a unused pcb from pcb[1..PROC_NUM-1], return NULL if no such one
  // TODO();
  // init ALL attributes of the pcb
  proc_t *p = NULL;
  for (int i = 1; i < PROC_NUM; ++i)
  {
    if (pcb[i].status == UNUSED)
    {
      p = &pcb[i];
      break;
    }
  }
  if (p == NULL)
    return NULL;
  p->pid = next_pid;
  ++next_pid;
  p->status = UNINIT;
  p->pgdir = vm_alloc();
  p->brk = 0;
  p->kstack = kalloc();
  p->ctx = &(p->kstack->ctx);
  p->parent = NULL;
  p->child_num = 0;
  sem_init(&p->zombie_sem, 0);
  for (int i = 0; i < MAX_USEM; ++i)
    p->usems[i] = NULL;
  for (int i = 0; i < MAX_UFILE; ++i)
    p->files[i] = NULL;
  p->cwd = NULL;
  return p;
}

void proc_free(proc_t *proc)
{
  // Lab2-1: free proc's pgdir and kstack and mark it UNUSED
  // TODO();
  if (proc == curr)
    return;
  vm_teardown(proc->pgdir);
  kfree(proc->kstack);
  proc->status = UNUSED;
}

proc_t *proc_curr()
{
  return curr;
}

void proc_run(proc_t *proc)
{
  proc->status = RUNNING;
  curr = proc;
  set_cr3(proc->pgdir);
  set_tss(KSEL(SEG_KDATA), (uint32_t)STACK_TOP(proc->kstack));
  irq_iret(proc->ctx);
}

void proc_addready(proc_t *proc)
{
  // Lab2-1: mark proc READY
  proc->status = READY;
}

void proc_yield()
{
  // Lab2-1: mark curr proc READY, then int $0x81
  curr->status = READY;
  INT(0x81);
}

void proc_copycurr(proc_t *proc)
{
  // Lab2-2: copy curr proc
  proc_t *curr_proc = proc_curr();
  vm_copycurr(proc->pgdir);
  proc->brk = curr_proc->brk;
  proc->kstack->ctx = curr_proc->kstack->ctx;
  proc->kstack->ctx.eax = 0;
  proc->parent = curr_proc;
  ++curr_proc->child_num;
  // Lab2-5: dup opened usems
  for (int i = 0; i < MAX_USEM; ++i)
  {
    if (curr_proc->usems[i] != NULL)
    {
      proc->usems[i] = usem_dup(curr_proc->usems[i]);
    }
  }
  // Lab3-1: dup opened files
  for (int i = 0; i < MAX_UFILE; ++i)
  {
    if (curr_proc->files[i] != NULL)
    {
      proc->files[i] = fdup(curr_proc->files[i]);
    }
  }
  // Lab3-2: dup cwd
  proc->cwd = idup(curr_proc->cwd);
  // TODO();
}

void proc_makezombie(proc_t *proc, int exitcode)
{
  // Lab2-3: mark proc ZOMBIE and record exitcode, set children's parent to NULL
  proc->status = ZOMBIE;
  proc->exit_code = exitcode;
  for (int i = 0; i < PROC_NUM; ++i)
    if (pcb[i].parent == proc)
      pcb[i].parent = NULL;
  if (proc->parent != NULL)
    sem_v(&proc->parent->zombie_sem);
  // Lab2-5: close opened usem
  for (int i = 0; i < MAX_USEM; ++i)
  {
    if (proc->usems[i] != NULL)
    {
      usem_close(proc->usems[i]);
    }
  }
  // Lab3-1: close opened files
  for (int i = 0; i < MAX_UFILE; ++i)
  {
    if (proc->files[i] != NULL)
    {
      fclose(proc->files[i]);
    }
  }
  // Lab3-2: close cwd
  iclose(proc->cwd);
  // TODO();
}

proc_t *proc_findzombie(proc_t *proc)
{
  // Lab2-3: find a ZOMBIE whose parent is proc, return NULL if none
  // TODO();
  for (int i = 0; i < PROC_NUM; ++i)
    if (pcb[i].parent == proc && pcb[i].status == ZOMBIE)
      return &pcb[i];
  return NULL;
}

void proc_block()
{
  // Lab2-4: mark curr proc BLOCKED, then int $0x81
  curr->status = BLOCKED;
  INT(0x81);
}

int proc_allocusem(proc_t *proc)
{
  // Lab2-5: find a free slot in proc->usems, return its index, or -1 if none
  // TODO();
  int index = -1;
  for (int i = 0; i < MAX_USEM; ++i)
  {
    if (proc->usems[i] == NULL)
    {
      index = i;
      break;
    }
  }
  return index;
}

usem_t *proc_getusem(proc_t *proc, int sem_id)
{
  // Lab2-5: return proc->usems[sem_id], or NULL if sem_id out of bound
  // TODO();
  if (sem_id >= MAX_USEM)
    return NULL;
  return proc->usems[sem_id];
}

int proc_allocfile(proc_t *proc)
{
  // Lab3-1: find a free slot in proc->files, return its index, or -1 if none
  // TODO();
  int index = -1;
  for (int i = 0; i < MAX_UFILE; ++i)
  {
    if (proc->files[i] == NULL)
    {
      index = i;
      break;
    }
  }
  return index;
}

file_t *proc_getfile(proc_t *proc, int fd)
{
  // Lab3-1: return proc->files[fd], or NULL if fd out of bound
  // TODO();
  if (fd >= MAX_UFILE)
    return NULL;
  return proc->files[fd];
}

void schedule(Context *ctx)
{
  // Lab2-1: save ctx to curr->ctx, then find a READY proc and run it
  // TODO();
  proc_t *p = proc_curr();
  p->ctx = ctx;
  if (p == &pcb[PROC_NUM - 1])
    p = &pcb[0];
  else
    ++p;
  while (p->status != READY)
  {
    if (p == &pcb[PROC_NUM - 1])
      p = &pcb[0];
    else
      ++p;
  }
  proc_run(p);
}
