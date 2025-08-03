#pragma PRINT_H

#include <stdint.h>

// Configuración VGA Text Mode
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000  // Dirección de memoria VGA donde escribimos en pantalla

// Paleta de colores VGA (16 colores predefinidos)
#define VGA_COLOR_BLACK      0
#define VGA_COLOR_BLUE       1
#define VGA_COLOR_GREEN      2
#define VGA_COLOR_CYAN       3
#define VGA_COLOR_RED        4
#define VGA_COLOR_MAGENTA    5
#define VGA_COLOR_BROWN      6
#define VGA_COLOR_LIGHT_GREY 7
#define VGA_COLOR_DARK_GREY  8
#define VGA_COLOR_LIGHT_BLUE 9
#define VGA_COLOR_LIGHT_GREEN 10
#define VGA_COLOR_LIGHT_CYAN 11
#define VGA_COLOR_LIGHT_RED  12
#define VGA_COLOR_PINK       13
#define VGA_COLOR_YELLOW     14
#define VGA_COLOR_WHITE      15

// ============ FUNCIONES BÁSICAS DE IMPRESIÓN ============

/*
 * Imprime una cadena de caracteres en la posición actual del cursor.
 * Procesa cada carácter hasta encontrar el terminador '\0'.
 * Maneja automáticamente saltos de línea y caracteres especiales.
 * 
 * @param str: Cadena terminada en null a imprimir
 */
void print(const char *str);

/*
 * Imprime un solo carácter manejando casos especiales:
 * - '\n': Nueva línea (cursor a inicio de siguiente línea)
 * - '\t': Tab (salta a próxima columna múltiplo de 8)  
 * - '\r': Carriage return (cursor a inicio de línea actual)
 * - ASCII >= 32: Caracteres imprimibles normales
 * - ASCII < 32: Ignorados (caracteres de control)
 * 
 * @param c: Carácter a imprimir
 */
void putchar(char c);

/*
 * Imprime un carácter en posición específica sin mover el cursor global.
 * Útil para escribir en posiciones exactas (HUDs, menús, etc.)
 * 
 * @param c: Carácter a imprimir
 * @param color: Atributo de color (usar make_color())
 * @param x: Columna (0-79)
 * @param y: Fila (0-24)
 */
void putchar_at(char c, unsigned char color, int x, int y);

// ============ FUNCIONES DE COLOR ============

/*
 * Crea un atributo de color combinando foreground y background.
 * Formato: BBBBFFFF (4 bits background + 4 bits foreground)
 * 
 * @param fg: Color de texto (0-15)
 * @param bg: Color de fondo (0-15)
 * @return: Byte de atributo de color
 */
unsigned char make_color(unsigned char fg, unsigned char bg);

/*
 * Imprime texto con colores específicos sin afectar el color global.
 * Restaura el color original después de imprimir.
 * 
 * @param str: Cadena a imprimir
 * @param fg: Color de texto
 * @param bg: Color de fondo
 */
void print_colored(const char *str, unsigned char fg, unsigned char bg);

// ============ FUNCIONES DE CONTROL DE PANTALLA ============

/*
 * Limpia toda la pantalla llenándola con espacios en blanco.
 * Resetea el cursor a posición (0,0).
 */
void clear_screen();

/*
 * Simula scroll hacia arriba cuando la pantalla se llena.
 * Copia línea N+1 → línea N para todas las líneas,
 * limpia la última línea y ajusta cursor_y.
 */
void scroll();

/*
 * Posiciona el cursor en coordenadas específicas.
 * Valida que las coordenadas estén dentro de los límites de pantalla.
 * 
 * @param x: Columna destino (0-79)
 * @param y: Fila destino (0-24)
 */
void set_cursor_pos(int x, int y);

// ============ FUNCIONES DE CONVENIENCIA ============

/*
 * Imprime mensaje de error en rojo.
 * Útil para debugging y logging de interrupciones.
 */
void print_error(const char *str);

/*
 * Imprime mensaje de advertencia en amarillo.
 */
void print_warning(const char *str);

/*
 * Imprime mensaje de éxito en verde.
 */
void print_success(const char *str);

