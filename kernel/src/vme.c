#include "klib.h"
#include "vme.h"
#include "proc.h"

//prot一般有5种：0（无效）、1或3（仅内核可读写）、5（内核可读写、用户只读）、7（内核和用户都可读写）U/S | R/W | P

static TSS32 tss;

void init_gdt() {
  static SegDesc gdt[NR_SEG];
  gdt[SEG_KCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_KERN);
  gdt[SEG_KDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_KERN);
  gdt[SEG_UCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_USER);
  gdt[SEG_UDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_USER);
  gdt[SEG_TSS]   = SEG16(STS_T32A,     &tss,  sizeof(tss)-1, DPL_KERN);
  set_gdt(gdt, sizeof(gdt[0]) * NR_SEG);
  set_tr(KSEL(SEG_TSS));
}

void set_tss(uint32_t ss0, uint32_t esp0) {
  tss.ss0 = ss0;
  tss.esp0 = esp0;
}

static PD kpd;
static PT kpt[PHY_MEM / PT_SIZE] __attribute__((used));

typedef union free_page {
  union free_page *next;
  char buf[PGSIZE];
} page_t;

page_t *free_page_list;


void init_page() {
  extern char end;
  panic_on((size_t)(&end) >= KER_MEM - PGSIZE, "Kernel too big (MLE)");
  static_assert(sizeof(PTE) == 4, "PTE must be 4 bytes");
  static_assert(sizeof(PDE) == 4, "PDE must be 4 bytes");
  static_assert(sizeof(PT) == PGSIZE, "PT must be one page");
  static_assert(sizeof(PD) == PGSIZE, "PD must be one page");
  // Lab1-4: init kpd and kpt, identity mapping of [0 (or 4096), PHY_MEM)
  //TODO();
  //init kpd
  for(int i=0;i<PHY_MEM / PT_SIZE;++i){
    kpd.pde[i].val=MAKE_PDE(&kpt[i],PDE_W|PDE_P);
  }
  //init kpt
  for(int pd_index=0;pd_index<PHY_MEM / PT_SIZE;++pd_index){
    for(int pt_index=0;pt_index<NR_PTE;++pt_index){
      kpt[pd_index].pte[pt_index].val=MAKE_PTE((pd_index<<DIR_SHIFT)|(pt_index<<TBL_SHIFT),PTE_W|PTE_P);
    }
  }

  kpt[0].pte[0].val = 0;
  set_cr3(&kpd);
  set_cr0(get_cr0() | CR0_PG);
  // Lab1-4: init free memory at [KER_MEM, PHY_MEM), a heap for kernel
  //TODO();
  free_page_list=NULL;
  for(uint32_t addr=KER_MEM;addr<PHY_MEM;addr+=PGSIZE){
    //链表头插
    page_t* temp=free_page_list;
    free_page_list=(page_t*)addr;
    free_page_list->next=temp;
  }
}

void *kalloc() {
  // Lab1-4: alloc a page from kernel heap, abort when heap empty
  //TODO();
  //链表头删
  assert(free_page_list!=NULL);
  page_t* temp=free_page_list->next;
  page_t* new_page=free_page_list;
  //new_page->next=NULL;
  free_page_list=temp;
  //将alloc页面内容清0
  memset(new_page,0,PGSIZE);
  return new_page;
}

void kfree(void *ptr) {
  // Lab1-4: free a page to kernel heap
  // you can just do nothing :)
  //TODO();
  //链表头插
  assert((void*)KER_MEM<=ptr && ptr<(void*)PHY_MEM);
  ptr=(void*)(PAGE_DOWN((uint32_t)ptr));
  //将free页面内容清0
  memset(ptr,0,PGSIZE);
  page_t* temp=free_page_list;
  free_page_list=(page_t*)ptr;
  free_page_list->next=temp;
}

PD *vm_alloc() {
  // Lab1-4: alloc a new pgdir, map memory under PHY_MEM identityly
  //TODO();
  PD* pgdir=(PD*)kalloc();
  for(int i=0;i<NR_PDE;++i)pgdir->pde[i].val=0;
  for(int i=0;i<PHY_MEM / PT_SIZE;++i){
    pgdir->pde[i].val=MAKE_PDE(&kpt[i],PDE_P);//prot?
  }
  return pgdir;
}

void vm_teardown(PD *pgdir) {
  // Lab1-4: free all pages mapping above PHY_MEM in pgdir, then free itself
  // you can just do nothing :)
  //TODO();
  for(int i=PHY_MEM / PT_SIZE;i<NR_PDE;++i){
    if(pgdir->pde[i].present==0)continue;
    PT* pt=PDE2PT(pgdir->pde[i]);
    for(int j=0;j<NR_PTE;++j){
      if(pt->pte[j].present==0)continue;
      kfree(PTE2PG(pt->pte[j]));
    }
    kfree(pt);
  }
  kfree(pgdir);
}

PD *vm_curr() {
  return (PD*)PAGE_DOWN(get_cr3());
}

PTE *vm_walkpte(PD *pgdir, size_t va, int prot) {
  // Lab1-4: return the pointer of PTE which match va
  // if not exist (PDE of va is empty) and prot&1, alloc PT and fill the PDE
  // if not exist (PDE of va is empty) and !(prot&1), return NULL
  // remember to let pde's prot |= prot, but not pte
  assert((prot & ~7) == 0);
  //TODO();
  assert(va>=PHY_MEM);
  uint32_t pd_index=ADDR2DIR(va);
  uint32_t pt_index=ADDR2TBL(va);
  PDE* pde=&pgdir->pde[pd_index];
  if(pde->present==0 && prot&0x1){
    PT* pt=kalloc();
    pde->val=MAKE_PDE(pt,prot);
    return &pt->pte[pt_index];
  }
  else if((prot&0x1)==0) return NULL;
  else{
    pde->val |= prot;
    PT* pt=PDE2PT(*pde);
    return &pt->pte[pt_index];
  }
}

void *vm_walk(PD *pgdir, size_t va, int prot) {
  // Lab1-4: translate va to pa
  // if prot&1 and prot voilation ((pte->val & prot & 7) != prot), call vm_pgfault
  // if va is not mapped and !(prot&1), return NULL
  //TODO();
  assert(va>=PHY_MEM);
  PTE* pte=vm_walkpte(pgdir,va,prot);
  if(pte==NULL || pte->present==0)return NULL;
  else return PTE2PG(*pte);
}

void vm_map(PD *pgdir, size_t va, size_t len, int prot) {
  // Lab1-4: map [PAGE_DOWN(va), PAGE_UP(va+len)) at pgdir, with prot
  // if have already mapped pages, just let pte->prot |= prot
  assert(prot & PTE_P);
  assert((prot & ~7) == 0);
  size_t start = PAGE_DOWN(va);
  size_t end = PAGE_UP(va + len);
  assert(start >= PHY_MEM);
  assert(end >= start);
  //TODO();
  for(uint32_t vaddr=start;vaddr<end;vaddr+=PGSIZE){
    PTE* pte=vm_walkpte(pgdir,vaddr,prot);
    if(pte->present==0){
      pte->val=MAKE_PTE(kalloc(),prot);
    }
    else{
      pte->val |= prot;
    }
  }
}

void vm_unmap(PD *pgdir, size_t va, size_t len) {
  // Lab1-4: unmap and free [va, va+len) at pgdir
  // you can just do nothing :)
  assert(ADDR2OFF(va) == 0);
  assert(ADDR2OFF(len) == 0);
  //TODO();
  size_t start = PAGE_DOWN(va);
  size_t end = PAGE_UP(va + len);
  assert(start >= PHY_MEM);
  assert(end >= start);
  for(uint32_t vaddr=start;vaddr<end;vaddr+=PGSIZE){
    uint32_t pd_index=ADDR2DIR(vaddr);
    PDE* pde=&pgdir->pde[pd_index];
    if(pde->present==0)continue;
    uint32_t pt_index=ADDR2TBL(vaddr);
    PT* pt=PDE2PT(*pde);
    PTE* pte=&pt->pte[pt_index];
    if(pte->present==0)continue;
    kfree(PTE2PG(*pte));
    pte->present=0;//this caused a bug
  }
  if(pgdir==vm_curr())set_cr3(vm_curr());
}

void vm_copycurr(PD *pgdir) {
  // Lab2-2: copy memory mapped in curr pd to pgdir
  //TODO();
  for(uint32_t vaddr=PHY_MEM;vaddr<USR_MEM;vaddr+=PGSIZE){
    PTE* pte=vm_walkpte(vm_curr(),vaddr,PDE_U|PDE_W|PDE_P);
    if(pte!=NULL&&pte->present==1){
      vm_map(pgdir,vaddr,PGSIZE,PDE_U|PDE_W|PDE_P);
      void* dst=vm_walk(pgdir,vaddr,PDE_U|PDE_W|PDE_P);
      void* src=vm_walk(vm_curr(),vaddr,PDE_U|PDE_W|PDE_P);
      memcpy(dst,src,PGSIZE);
    }
  }
}

void vm_pgfault(size_t va, int errcode) {
  printf("pagefault @ 0x%p, errcode = %d\n", va, errcode);
  panic("pgfault");
}
