#ifndef EEP_BACKEND_H
#define EEP_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void eep_backend_init(void);
void eep_backend_event_handler(void);
const uint8_t *eep_backend_get_image(uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif /* EEP_BACKEND_H */
