#ifndef QRCODE_H
#define QRCODE_H

#include <stdint.h>

#define QR_MAX_MODULES 33

/*
 * Encode text into a QR code (byte mode, EC level L).
 *   text      – input bytes
 *   len       – length (max ~78 for version 4)
 *   modules   – output array, QR_MAX_MODULES*QR_MAX_MODULES bytes
 *               each byte is 0 (white) or 1 (black)
 *   out_size  – receives the side length of the QR code
 * Returns the QR version (1-4) on success, 0 on failure.
 */
int qr_encode(const char *text, int len, uint8_t *modules, int *out_size);

#endif /* QRCODE_H */
