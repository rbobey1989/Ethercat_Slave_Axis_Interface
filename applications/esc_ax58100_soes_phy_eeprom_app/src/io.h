/* io.h - simple GPIO I/O used by PDO callbacks */
#ifndef IO_H
#define IO_H

#include <stdint.h>

#define IO_TEMP_OUTPUT_CHANNELS 16U
#define IO_TEMP_INPUT_CHANNELS  16U

void io_init(void);
void cb_set_outputs(void);
void cb_get_inputs(void);

#endif /* IO_H */
