/*
 * pydos_io.h - DOS I/O operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_IO_H
#define PDOS_IO_H

#include "pdos_obj.h"

/* Write far data to stdout via INT 21h AH=40h BX=1 */
int PYDOS_API pydos_dos_write_far(const char far *data, unsigned int len);

/* Write near string to stdout */
int PYDOS_API pydos_dos_write(const char *data, unsigned int len);

/* Single character output via INT 21h AH=02h */
void PYDOS_API pydos_dos_putchar(char c);

/* Read line from stdin via INT 21h AH=01h, char by char until CR */
unsigned int PYDOS_API pydos_dos_readline(char far *buf, unsigned int maxlen);

/* Print null-terminated far string to stdout */
void PYDOS_API pydos_dos_print_str(const char far *str);

/* File open: mode 0=read, 1=write(create), 2=read/write. Returns handle or -1 */
int PYDOS_API pydos_dos_file_open(const char far *path, unsigned char mode);

/* File close */
void PYDOS_API pydos_dos_file_close(int handle);

/* File read. Returns bytes actually read, or -1 on error */
int PYDOS_API pydos_dos_file_read(int handle, char far *buf, unsigned int count);

/* File write. Returns bytes actually written, or -1 on error */
int PYDOS_API pydos_dos_file_write(int handle, const char far *data, unsigned int count);

void PYDOS_API pydos_io_init(void);
void PYDOS_API pydos_io_shutdown(void);

#endif /* PDOS_IO_H */
