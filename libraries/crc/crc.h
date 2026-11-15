#ifndef CRC_H
#define CRC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PACKET_SOF 0xAA
#define PACKET_EOF 0x55

/* Protocol version */
enum protocol_version {
    VER_1 = 0x01,
    // Not implemented yet another versions
};

enum packet_type {
    PING = 0x01,   // Link test request, empty payload
    PONG = 0x02,   // Link test reply, empty payload
    TEMP = 0x10,   // Temperature data, int16 centi-degrees
    STATUS = 0x11, // Status data
    GET = 0x12,    // Ask for one temperature, empty payload
    ERROR = 0x99,  // Error packet
};

enum error_code {
    ERR_NONE = 0x00,    // No error
    ERR_CRC = 0x01,     // CRC error
    ERR_UNKNOWN = 0xFF, // Unknown error
};

enum status_code {
    STATUS_OK = 0x00,      // Status OK
    STATUS_WORKING = 0x01, // Status working
    STATUS_IDLE = 0x02,    // Status idle
    STATUS_ERROR = 0xFF,   // Status error
};

uint16_t crc16_modbus(const uint8_t *data, size_t length);
uint16_t build_packet(uint8_t protocol, uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *packet);
uint16_t change_temp(float temp, uint8_t *packet);
float bytes_to_float(uint8_t lsb, uint8_t msb);
bool packet_decode(const uint8_t *packet, uint8_t *type, float *temperature);
bool packet_read_temp(const uint8_t *packet, float *temperature);
void parse_packet(const uint8_t *packet);

#endif
