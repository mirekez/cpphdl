#define _GNU_SOURCE
#include <link.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

/* Single-threaded Rocket runners: statistical CPU-time samples, 16-byte bins.
 * No allocation, I/O, unwinding, or symbol lookup in the signal handler. */
static uintptr_t base, lo, hi;
static volatile sig_atomic_t *bins;
static volatile sig_atomic_t outside, total;
static int active;
static int locate(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size; (void)data;
  if (info->dlpi_name[0]) return 0;
  base = info->dlpi_addr;
  for (int i=0; i<info->dlpi_phnum; ++i) {
    const ElfW(Phdr) *p=&info->dlpi_phdr[i];
    if (p->p_type == PT_LOAD && (p->p_flags & PF_X)) {
      lo=base+p->p_vaddr; hi=lo+p->p_memsz;
      bins=calloc((hi-lo+15)/16, sizeof(*bins));
      break;
    }
  }
  return 1;
}
static void sample(int sig, siginfo_t *info, void *context) {
  (void)sig; (void)info;
  uintptr_t pc=((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP];
  ++total;
  if(pc>=lo && pc<hi) ++bins[(pc-lo)/16]; else ++outside;
}
__attribute__((constructor)) static void start(void) {
  if(!getenv("CPU_PROFILE_OUT")) return;
  dl_iterate_phdr(locate, 0);
  if(!bins) return;
  struct sigaction action={0};
  action.sa_sigaction=sample; action.sa_flags=SA_SIGINFO|SA_RESTART;
  sigemptyset(&action.sa_mask);
  if(sigaction(SIGPROF,&action,0)) return;
  /* ITIMER_PROF resolution can be limited by the kernel tick rate. */
  struct itimerval timer={{0,1000},{0,1000}};
  if(!setitimer(ITIMER_PROF,&timer,0)) active=1;
}
__attribute__((destructor)) static void finish(void) {
  if(!active) return;
  struct itimerval stopped={0}; setitimer(ITIMER_PROF,&stopped,0);
  FILE *f=fopen(getenv("CPU_PROFILE_OUT"),"w");
  if(!f) return;
  fprintf(f,"# total %d outside_executable %d stride 16 interval_us 1000\n",total,outside);
  for(size_t i=0;i<(hi-lo+15)/16;++i)
    if(bins[i]) fprintf(f,"%lx %d\n",(unsigned long)(lo-base+i*16),bins[i]);
  fclose(f);
}
