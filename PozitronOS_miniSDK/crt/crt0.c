void _start(void);
int main(int argc, char** argv);

void _start(void) {
    int ret;
    
    asm volatile(
        "pusha\n"
        "pushl $0\n"
        "pushl $0\n"
        "call main\n"
        "addl $8, %%esp\n"
        "movl %%eax, %0\n"
        "popa\n"
        : "=r"(ret)
        :
        : "memory"
    );
    
    asm volatile("int $0x80" : : "a"(1), "b"(ret));
    
    while(1);
}