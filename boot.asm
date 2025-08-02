; boot.asm
; Entrada compatible con mi propio bootloader.

[BITS 32]
[EXTERN Kernel_main]

section .text
global _start

_start:
    cli 
    call Kernel_main

.hang:
    hlt
    jmp .hang ; Quedate tildado si no hay instrucciones nuevas (como un fallback por si en alto nivel tenemos un retorno inválido)
            


; Esto es el punto real de entrada del sistema donde permitimos a los módulos de C funcionar, por eso no usamos main() 
;como punto de entrada, ya que no tenemos runtime para matar procesos y definir su ciclo de vida como en los programas típicos de 
;usuario donde prima: loader → _start → main() → return 0 → exit(0) → syscall exit → SO libera recursos
;acá sería algo como GRUB → _start → Kernel_Main() → lo que sea que haga esa funcion. 
; Una vez que se llama a kernel_main, no hay retorno posible.
; No existe un sistema operativo que nos esté esperando, ni una syscall exit()
; como en el espacio de usuario. Somos el único código ejecutándose.
; Si kernel_main termina, ejecutamos un loop con hlt → jmp .hang
; para evitar comportamiento indefinido.
;
; Si queremos terminar el sistema, debemos apagarlo manualmente (out 0x604, al)
; o reiniciarlo (out 0x64, al con 0xFE). Por ahora, simplemente colgamos la CPU.
;