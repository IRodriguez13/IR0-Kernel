#include "panic.h"
#include "<ir0/print.h>"

// IR0 Advanced Panic Handler
// Convertido a sintaxis Intel para mejor legibilidad

// Niveles de panic semánticos
typedef enum 
{
    PANIC_KERNEL_BUG = 0,     // Bug en kernel code
    PANIC_HARDWARE_FAULT,     // Hardware malfunction
    PANIC_OUT_OF_MEMORY,      // System out of memory
    PANIC_STACK_OVERFLOW,     // Stack corruption
    PANIC_ASSERT_FAILED       // Assertion failure

} panic_level_t;


static const char* panic_level_names[] = 
{
    "KERNEL BUG",
    "HARDWARE FAULT", 
    "OUT OF MEMORY",
    "STACK OVERFLOW",
    "ASSERTION FAILED"
};

// Nos fijamos si estamos en doble panic
static volatile int in_panic = 0;

// Utilizamos un mejor manejo del stack-trace 
void panic_advanced(const char *message, panic_level_t level, const char *file, int line)
{
    // Por si de casualidad también falla Panic.
    if (in_panic) 
    {
        print_error("DOUBLE PANIC! System completely fucked.\n");
        cpu_relax();
        return;
    }
    
    in_panic = 1;
    
    // Cortamos las interrupciones inmediatamente.
    asm volatile("cli");
    
    // Limpio la pantalla 
    clear_screen();
    
    // Header con timestamp pero para timer.
    print_colored("╔═══════════════════════════════════════════════════════════╗\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("║                     KERNEL PANIC                          ║\n", VGA_COLOR_WHITE, VGA_COLOR_RED);
    print_colored("╚═══════════════════════════════════════════════════════════╝\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    
    print("\n");
    
    // Panic info básica
    print_colored("Type: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(panic_level_names[level]);
    print("\n");
    
    print_colored("Location: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print(file);
    print(":");
    print_hex_compact(line);
    print("\n");
    
    print_colored("Message: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(message);
    print("\n\n");
    
    // Me guardo una foto de los registros en el momento del panic
    dump_registers();
    
    // Stack trace
    dump_stack_trace();
    
    // informacion de la memoria
    dump_memory_info();
    
    // Mensaje antes del cpu_relax
    print_colored("\n═══ SYSTEM HALTED ═══\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("Safe to power off or reboot\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    cpu_relax(); // la cpu a hacer noni para que no haya mas problemas intrackeables.
}

void dump_registers()
{
    uint32_t eax, ebx, ecx, edx, esp, ebp, eip, eflags, cr0, cr2, cr3;
    
    // Capturar registros actuales en el momento del error - Sintaxis Intel porque me parece mas legible 
    asm volatile(
        "mov %0, eax\n\t"
        "mov %1, ebx\n\t" 
        "mov %2, ecx\n\t"
        "mov %3, edx\n\t"
        "mov %4, esp\n\t"
        "mov %5, ebp\n\t"
        "pushfd\n\t"              // Push EFLAGS
        "pop %7\n\t"              // Pop a variable
        "mov eax, cr0\n\t"
        "mov %8, eax\n\t"
        "mov eax, cr2\n\t"        // Fault address
        "mov %9, eax\n\t"
        "mov eax, cr3\n\t"        // Page directory
        "mov %10, eax\n\t"
        : "=m"(eax), "=m"(ebx), "=m"(ecx), "=m"(edx),
          "=m"(esp), "=m"(ebp), "=m"(eip), "=m"(eflags),
          "=m"(cr0), "=m"(cr2), "=m"(cr3)
        :
        : "eax"
    );
    
    print_colored("--- REGISTER DUMP ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    print("EAX: "); print_hex_compact(eax); print("  ");
    print("EBX: "); print_hex_compact(ebx); print("\n");
    
    print("ECX: "); print_hex_compact(ecx); print("  ");
    print("EDX: "); print_hex_compact(edx); print("\n");
    
    print("ESP: "); print_hex_compact(esp); print("  ");
    print("EBP: "); print_hex_compact(ebp); print("\n");
    
    print("CR0: "); print_hex_compact(cr0); print("  ");
    print("CR2: "); print_hex_compact(cr2); print("  ");
    print("CR3: "); print_hex_compact(cr3); print("\n");
    
    print("EFLAGS: "); print_hex_compact(eflags); print("\n\n");
}

void dump_stack_trace()
{
    print_colored("--- STACK TRACE ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    uint32_t *ebp;
    
    asm volatile("mov %0, ebp" : "=r"(ebp)); 
    
    int frame_count = 0;
    const int max_frames = 10;
    
    while (ebp && frame_count < max_frames) 
    {
        // Validar que ebp esté en rango de memoria válido
        if ((uint32_t)ebp < 0x100000 || (uint32_t)ebp > 0x40000000) 
        {
            print_warning("Stack trace truncated (invalid frame pointer)\n");
            break;
        }
        
        uint32_t return_addr = *(ebp + 1);
        
        print("#");
        print_hex_compact(frame_count);
        print(": ");
        print_hex_compact(return_addr);
        print("\n");
        
        ebp = (uint32_t*)*ebp;  // Siguiente marco de pila
        frame_count++;
    }
    
    if (frame_count == 0) 
    {
        print_warning("No stack trace available\n");
    }
    
    print("\n");
}

void dump_memory_info()
{
    print_colored("--- MEMORY INFO ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    // Info básica de memoria (Lo tengo que adaptar cuando tenga Stack/heap)
    extern uint32_t free_pages_count, total_pages_count;
    
    uint32_t used_pages = total_pages_count - free_pages_count;
    uint32_t mem_usage_percent = (used_pages * 100) / total_pages_count;
    
    print("Total memory: ");
    print_hex_compact(total_pages_count * 4096);
    print(" bytes\n");
    
    print("Free memory: ");  
    print_hex_compact(free_pages_count * 4096);
    print(" bytes\n");
    
    print("Memory usage: ");
    print_hex_compact(mem_usage_percent);
    print("%\n\n");
}

// Macros para facilidad en el panic event
#define BUG_ON(condition) \
    do { \
        if (unlikely(condition)) \
        { \
            panic_advanced("BUG_ON: " #condition, PANIC_KERNEL_BUG, __FILE__, __LINE__); \
        } \
    } while(0)

#define ASSERT(condition) \
    do { \
        if (unlikely(!(condition))) \
        { \
            panic_advanced("ASSERT failed: " #condition, PANIC_ASSERT_FAILED, __FILE__, __LINE__); \
        } \
    } while(0)
      
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)


// panic() original como wrapper. Así no tengo que reemplazarlo en cada llamado.
void panic(const char *message)
{
    panic_advanced(message, PANIC_KERNEL_BUG, "unknown", 0);
}


// cpu_relax - halt es una instruccion que corta cualquier ejecucion de la cpu, hasta la siguiente interrupcion
// en este caso, la cpu entre en ese trance indefinidamente.

void cpu_relax()
{
    for(;;)
    {
        asm volatile("hlt");
    }
}

