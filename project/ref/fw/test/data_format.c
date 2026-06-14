#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#define DSM_SIGNATURE (0xADU)
#define DSM_PACKET_SIZE_BYTES (82U)

#pragma pack(push, 2)

typedef struct
{
    uint8_t sig;
    uint8_t n;

    int16_t adc_in;
    int16_t fgen_out;
    int16_t filt_x;
    int16_t filt_y;
    int16_t dac0;
    int16_t dac1;
    int16_t dt;

    float aux0;
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
    float aux15;

    uint16_t crc16;

} dsm_packet_t;

#pragma pack(pop)

_Static_assert(
    sizeof(dsm_packet_t) == DSM_PACKET_SIZE_BYTES,
    "DSM size invalid");

static uint16_t dsm_calc_xor16(uint8_t *buf, int n)
{
    uint16_t y = 0;

    uint16_t *pbuf = (uint16_t *)buf;

    for (int i = 0; i < (n >> 1); i++)
    {
        y ^= *pbuf++;
    }

    return y;
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t max_len)
{
    size_t len = strlen(hex);
    printf("hex_len: %d\n", len);

    if ((len % 2) != 0)
    {
        return -1;
    }

    size_t byte_count = len / 2;

    if (byte_count > max_len)
    {
        return -1;
    }

    for (size_t i = 0; i < byte_count; i++)
    {
        char tmp[3];

        tmp[0] = hex[i * 2];
        tmp[1] = hex[i * 2 + 1];
        tmp[2] = 0;

        out[i] = (uint8_t)strtoul(tmp, NULL, 16);
    }

    return (int)byte_count;
}

static void dsm_print_raw_bytes(const uint8_t *data, size_t offset, size_t length)
{
    printf("0x%02zu : ", offset);

    for (size_t i = 0; i < length; i++)
    {
        printf("%02x ", data[offset + i]);
    }

    printf("\n");
}

static void dsm_decode_packet(const dsm_packet_t *pkt)
{
    const uint8_t *raw = (const uint8_t *)pkt;

    uint8_t seq;

    seq = (pkt->n >> 6) & 0x03;

    printf("\n");
    printf("==================================================\n");
    printf("DSM PACKET DECODE\n");
    printf("==================================================\n");

    // --------------------------------------------------
    // HEADER
    // --------------------------------------------------

    dsm_print_raw_bytes(raw, 0, 2);

    printf("sig      : 0x%02X\n", pkt->sig);
    printf("n        : 0x%02X\n", pkt->n);
    printf("seq      : %u\n", seq);

    printf("\n");

    // --------------------------------------------------
    // INT16 SIGNALS
    // --------------------------------------------------

    dsm_print_raw_bytes(raw, 2, 14);

    printf("adc_in   : %d\n", pkt->adc_in);
    printf("fgen_out : %d\n", pkt->fgen_out);
    printf("filt_x   : %d\n", pkt->filt_x);
    printf("filt_y   : %d\n", pkt->filt_y);
    printf("dac0     : %d\n", pkt->dac0);
    printf("dac1     : %d\n", pkt->dac1);
    printf("dt       : %d us\n", pkt->dt);

    printf("\n");

    // --------------------------------------------------
    // FLOAT SIGNALS
    // --------------------------------------------------

    for (int i = 0; i < 16; i++)
    {
        const float *pf =
            &pkt->aux0 + i;

        size_t offset =
            offsetof(dsm_packet_t, aux0) +
            (i * sizeof(float));

        dsm_print_raw_bytes(raw, offset, 4);

        printf(
            "aux%-2d    : %f\n",
            i,
            *pf);
    }

    printf("\n");

    // --------------------------------------------------
    // CRC
    // --------------------------------------------------

    dsm_print_raw_bytes(raw, 80, 2);

    uint16_t calc_crc;

    dsm_packet_t tmp;

    memcpy(&tmp, pkt, sizeof(tmp));

    tmp.crc16 = 0;

    calc_crc =
        dsm_calc_xor16((uint8_t *)&tmp.sig, DSM_PACKET_SIZE_BYTES - 2);

    printf("crc16 rx : 0x%04X\n", pkt->crc16);
    printf("crc16 cl : 0x%04X\n", calc_crc);

    if (calc_crc == pkt->crc16)
    {
        printf("CRC      : VALID\n");
    }
    else
    {
        printf("CRC      : INVALID\n");
    }

    printf("==================================================\n");
}

int main(void)
{
    // --------------------------------------------------
    // PUT YOUR DSM HEX STRING HERE
    // --------------------------------------------------

    const char *hex_data =
        "add20000000000000000000000004f0204d241bd04d241bd0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e2d0";
    // seq0
    // "ad120000000000000000000000004f02393eddbc393eddbc0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e210";

    // seq1
    // "ad520000000000000000000000004f02f0834bbbf0834bbb0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e250";
    // seq2
    // "ad920000000000000000000000004f021b2e7e3c1b2e7e3c0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e290";

    // seq3
    // "add20000000000000000000000004f0204d241bd04d241bd0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e2d0";

    // --------------------------------------------------

    uint8_t raw[DSM_PACKET_SIZE_BYTES];

    int ret;

    ret = hex_to_bytes(hex_data, raw, sizeof(raw));

    if (ret < 0)
    {
        printf("hex parse failed\n");

        return -1;
    }

    if (ret != DSM_PACKET_SIZE_BYTES)
    {
        printf(
            "invalid DSM size: %d bytes\n",
            ret);

        return -1;
    }

    dsm_packet_t pkt;

    memcpy(
        &pkt,
        raw,
        sizeof(pkt));

    dsm_decode_packet(&pkt);

    return 0;
}