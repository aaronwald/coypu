#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "libbpf.h"
#include "bpf_load.h"

int main(int argc, char **argv)
{
    if (load_bpf_file("src/kern/nutria_mmap.o"))
    {
        printf("Error:'%s'\n", bpf_log_buf);
        return 1;
    }

    read_trace_pipe();

    return 0;
}
