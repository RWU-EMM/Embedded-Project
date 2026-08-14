#ifndef DSM_PACKET_H
#define DSM_PACKET_H

#include <stdint.h>
#include <stdio.h>

#define SEND_MODE_USB 0
#define SEND_MODE_UDP 1

#define ACTIVE_SEND_MODE SEND_MODE_UDP

#define UDP_BATCH_SIZE 100  

#define DSM_SIGNATURE (0xADU)

#if (ACTIVE_SEND_MODE == SEND_MODE_UDP)
#define DSM_PACKET_SIZE_BYTES (20U)
#else
#define DSM_PACKET_SIZE_BYTES (18U)
#endif

#if (ACTIVE_SEND_MODE == SEND_MODE_UDP)
#pragma pack(push, 1)
typedef struct
{
    uint32_t batch_id;
    uint16_t count;
} dsm_batch_hdr_t;
#pragma pack(pop)
#endif

#pragma pack(push, 2)

typedef struct
{
    uint8_t sig;
    uint8_t n;

#if (ACTIVE_SEND_MODE == SEND_MODE_UDP)
    uint16_t seq;
#endif

    int16_t adc_in;
    int16_t fgen_out;
    int16_t filt_x;
    int16_t filt_y;
    int16_t dac0;
    int16_t dac1;
    int16_t dt;

    /*float aux0;
    float aux1;
    float aux2;
    float aux3;
    float aux4;
    float aux5;
    float aux6;
    float aux7;

    float aux8;
    float aux9;
    float aux10;
    float aux11;
    float aux12;
    float aux13;
    float aux14;
    float aux15;*/

    uint16_t crc16;

} dsm_packet_t;

#pragma pack(pop)

uint16_t dsm_calc_xor16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0;

    const uint16_t *p = (const uint16_t *)buf;

    for (size_t i = 0; i < (len >> 1U); i++)
    {
        crc ^= p[i];
    }

    return crc;
}

#endif // DSM_PACKET_H