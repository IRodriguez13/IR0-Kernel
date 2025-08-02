// kernel.c
void print(const char* str) 
{

    unsigned short* VideoMemory = (unsigned short*)0xB8000;
    int i = 0;
    
    while (str[i] != 0) 
    {
        VideoMemory[i] = (VideoMemory[i] & 0xFF00) | str[i];
        i++;
    }
}

void kernel_main() //Literal este es el punto de entrada del kernel desde GRUB de booteo que se conforma en la rutina asm 
{
    print("Hola desde el kernel didactico! :-)");

    /* yo podría dormir la cpu sin instrucciones con asm volatile ("hlt"); de manera infinita, pero no me sirve si después 
    quiero meter mas funcionalidades como el coordinador, interrupciones, paginado */
   
    while (1) {} // Loop eterno
}
