#include <stdio.h>
#include "crc.h"

#define PACKET_MAX_PAYLOAD 16

/* Function helps to calculate crc16 */
uint16_t crc16_modbus(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/* Function used to build packet with generated data */
uint16_t build_packet(uint8_t protocol, uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *packet)
{
    uint16_t index = 0;

    packet[index++] = PACKET_SOF;
    packet[index++] = protocol;
    packet[index++] = type;
    packet[index++] = payload_len;

    for (uint8_t i = 0; i < payload_len; i++) {
        packet[index++] = payload[i];
    }

    uint16_t crc = crc16_modbus(&packet[1], index - 1);

    packet[index++] = crc & 0xFF;
    packet[index++] = (crc >> 8) & 0xFF;
    packet[index++] = PACKET_EOF;

    return index;
}

/* Function to change float value of temp to bytes */
uint16_t change_temp(float temp, uint8_t *packet)
{
    uint8_t payload[2];
    int16_t temp_int = (int16_t)(temp * 100);

    payload[0] = (uint8_t)(temp_int & 0xFF);
    payload[1] = (uint8_t)((temp_int >> 8) & 0xFF);

    return build_packet(VER_1, TEMP, payload, sizeof(payload), packet);
}

float bytes_to_float(uint8_t lsb, uint8_t msb)
{
    int16_t temp_int = (int16_t)(lsb | (msb << 8));
    return (float)temp_int / 100.0f;
}

bool packet_decode(const uint8_t *packet, uint8_t *type, float *temperature)
{
    uint8_t payload_len = packet[3];

    if (payload_len > PACKET_MAX_PAYLOAD) {
        return false;
    }
    if (packet[0] != PACKET_SOF || packet[payload_len + 6] != PACKET_EOF) {
        return false;
    }
    if (packet[1] != VER_1) {
        return false;
    }

    uint16_t crc_received = (uint16_t)packet[payload_len + 4] |
                            ((uint16_t)packet[payload_len + 5] << 8);
    uint16_t crc_calculated = crc16_modbus(&packet[1], (size_t)payload_len + 3);

    if (crc_received != crc_calculated) {
        printf("CRC error: received 0x%04X, calculated 0x%04X\n", crc_received, crc_calculated);
        return false;
    }

    switch (packet[2]) {
        case PING:
        case PONG:
        case GET:
            if (payload_len != 0) {
                return false;
            }
            break;
        case TEMP:
            if (payload_len != 2 || temperature == NULL) {
                return false;
            }
            *temperature = bytes_to_float(packet[4], packet[5]);
            break;
        default:
            return false;
    }

    if (type != NULL) {
        *type = packet[2];
    }
    return true;
}

bool packet_read_temp(const uint8_t *packet, float *temperature)
{
    uint8_t type;

    if (!packet_decode(packet, &type, temperature)) {
        return false;
    }
    return type == TEMP;
}

void parse_packet(const uint8_t *packet)
{
    uint8_t type;
    float temperature = 0.0f;

    if (!packet_decode(packet, &type, &temperature)) {
        printf("Invalid packet\n");
        return;
    }
    if (type == TEMP) {
        printf("Temperature: %.2f C\n", temperature);
    } else {
        printf("Packet type 0x%02X\n", type);
    }
}
