#include <linux/filter.h>
#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

SEC("kprobe/ksys_read")

// do
int bpf_prog1(struct pt_regs *ctx)
{
  unsigned long long size;
  char fmt[] = "sys_read size %d %d\n";
  u32 pid;
  pid = bpf_get_current_pid_tgid();
  bpf_probe_read(&size, sizeof(size), (void *)&PT_REGS_PARM3(ctx));
  
    if (pid == 11681 || pid == 11682)
      bpf_trace_printk(fmt, sizeof(fmt), size, pid);
  
  return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
