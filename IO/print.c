#include "print.h"


// Variables globales para trackear posición
static int cursor_x = 0;
static int cursor_y = 0;
static unsigned char current_color = 0x0F; // Blanco sobre negro

// Función para crear atributo de color
unsigned char make_color(unsigned char fg, unsigned char bg) 
{
    return fg | bg << 4;
}

// Función para poner un carácter en posición específica
void putchar_at(char c, unsigned char color, int x, int y) 
{
    unsigned short *vga = (unsigned short *)VGA_MEMORY;
    int index = y * VGA_WIDTH + x;
    vga[index] = (unsigned short)c | (unsigned short)color << 8;
}

// Función para scroll (mover todo una línea arriba)
void scroll() 
{
    unsigned short *vga = (unsigned short *)VGA_MEMORY;
    
    // Mover todas las líneas una posición arriba
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) 
    {
        vga[i] = vga[i + VGA_WIDTH];
    }
    
    // Limpiar la última línea
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) 
    {
        vga[i] = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK) << 8 | ' ';
    }
    
    cursor_y = VGA_HEIGHT - 1;
}

// Función para poner un carácter (con cursor automático)
void putchar(char c) 
{
    if (c == '\n') // el famoso "salto de línea"
    {
        cursor_x = 0;
        cursor_y++;
    
    } else if (c == '\t') 
    {
        cursor_x = (cursor_x + 8) & ~(8 - 1); // Salta a cualquier cloumna múltiplo de 8. TAB le decimos
    
    } else if (c == '\r') // vuelve al principio de línea sobreescribiendo lo que haya
    {
        cursor_x = 0;

    } else if (c >= ' ') // Sólo acepta Caracteres imprimibles ASCII 32 en adelante (los anteriores son corruptos para la impresion).
    { 
        putchar_at(c, current_color, cursor_x, cursor_y);
        cursor_x++;
    }
    
    // Nueva línea si llegamos al final, que sería la columna 79 o la 80-1
    if (cursor_x >= VGA_WIDTH) 
    {
        cursor_x = 0;
        cursor_y++;
    }
    
    // Scroll si llegamos al final de la pantalla
    if (cursor_y >= VGA_HEIGHT) 
    {
        scroll();
    }
}

// Tu función original mejorada
void print(const char *str) 
{
    int i = 0;
    while (str[i] != '\0') 
    {
        putchar(str[i]);
        i++;
    }
}

// Funciones útiles adicionales
void print_colored(const char *str, unsigned char fg, unsigned char bg) 
{
    unsigned char old_color = current_color;
    current_color = make_color(fg, bg);
    print(str);
    current_color = old_color;
}

void clear_screen() 
{
    unsigned short *vga = (unsigned short *)VGA_MEMORY;
    unsigned short blank = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK) << 8 | ' ';
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) 
    {
        vga[i] = blank;
    }
    
    cursor_x = 0;
    cursor_y = 0;
}

// Función para printf básico (sin implementar aquí, pero la idea)
void set_cursor_pos(int x, int y) 
{
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) 
    {
        cursor_x = x;
        cursor_y = y;
    }
}

void print_error(const char *str) // Utilizo un puntero al array de chars porque es más rápido que referenciar el string sólo (qu tambien es un array de chars, pero que recibe la direcc al primer elemento en ese caso)
{
    print_colored(str, VGA_COLOR_RED, VGA_COLOR_BLACK);
}

void print_warning(const char *str) // Y lo hago constante porque no tiene por qué cambiar el mensaje en runtime viste.
{
    print_colored(str,VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
}

void print_success(const char *str)
{
    print_colored(str, VGA_COLOR_GREEN, VGA_COLOR_BLACK);
}

void print_hex_compact(uint32_t num)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];
    int started = 0;
    int index = 0;

    print("0x");

    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (num >> (i * 4)) & 0xF;
        if (nibble != 0 || started || i == 0) {
            char c = hex_chars[nibble];
            buffer[index++] = c;
            started = 1;
        }
    }

    buffer[index] = '\0';
    print(buffer);
}
