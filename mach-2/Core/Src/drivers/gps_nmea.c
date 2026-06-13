/*
 * gps_nmea.c
 * Minimal NMEA $GPRMC / $GNRMC parser.
 *
 * $GPRMC sentence format:
 *   $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*hh<CR><LF>
 *   Field 0: $GPRMC
 *   Field 1: UTC time
 *   Field 2: Status A=active V=void
 *   Field 3: Latitude  ddmm.mmmm
 *   Field 4: N/S
 *   Field 5: Longitude dddmm.mmmm
 *   Field 6: E/W
 */

#include "drivers/gps_nmea.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ── NMEA ddmm.mmmm → decimal degrees ───────────────────────────────── */
static float nmea_to_decimal(const char *raw, char dir)
{
    if (raw == NULL || raw[0] == '\0') return 0.0f;

    float val = strtof(raw, NULL);
    int   deg = (int)(val / 100.0f);
    float min = val - (float)(deg * 100);
    float dec = (float)deg + min / 60.0f;

    if (dir == 'S' || dir == 'W') dec = -dec;
    return dec;
}

/* ── NMEA checksum validator ─────────────────────────────────────────── */
static bool nmea_checksum_ok(const char *sentence)
{
    /* Format: $...*XX  — XOR of bytes between $ and * */
    const char *p = sentence + 1;   /* skip $ */
    uint8_t calc = 0;
    while (*p && *p != '*') calc ^= (uint8_t)*p++;

    if (*p != '*') return false;     /* no checksum present — accept anyway */
    p++;                             /* skip * */

    uint8_t recv = (uint8_t)strtol(p, NULL, 16);
    return (calc == recv);
}

/* ── Extract nth comma-delimited field from sentence ─────────────────── */
static void get_field(const char *sentence, int n, char *out, int maxlen)
{
    int  field = 0;
    int  i     = 0;
    int  j     = 0;

    out[0] = '\0';
    while (sentence[i] != '\0') {
        if (sentence[i] == ',') {
            if (field == n) { out[j] = '\0'; return; }
            field++;
            j = 0;
        } else if (field == n) {
            if (j < maxlen - 1) out[j++] = sentence[i];
        }
        i++;
    }
    /* end of string */
    if (field == n) out[j] = '\0';
}

/* ── Public API ──────────────────────────────────────────────────────── */
bool GPS_ParseRMC(const char *sentence, GPS_Data_t *out)
{
    out->valid = false;

    /* Accept $GPRMC and $GNRMC */
    if (strncmp(sentence, "$GPRMC", 6) != 0 &&
        strncmp(sentence, "$GNRMC", 6) != 0) return false;

    /* Optional checksum validation */
    if (!nmea_checksum_ok(sentence)) return false;

    char field[16];

    /* Field 2: status A=active */
    get_field(sentence, 2, field, sizeof(field));
    if (field[0] != 'A') return false;   /* void fix */

    /* Field 3: latitude */
    char lat_raw[16], lat_dir[4];
    get_field(sentence, 3, lat_raw,  sizeof(lat_raw));
    get_field(sentence, 4, lat_dir,  sizeof(lat_dir));

    /* Field 5: longitude */
    char lon_raw[16], lon_dir[4];
    get_field(sentence, 5, lon_raw,  sizeof(lon_raw));
    get_field(sentence, 6, lon_dir,  sizeof(lon_dir));

    if (lat_raw[0] == '\0' || lon_raw[0] == '\0') return false;

    out->latitude  = nmea_to_decimal(lat_raw, lat_dir[0]);
    out->longitude = nmea_to_decimal(lon_raw, lon_dir[0]);
    out->valid     = true;
    return true;
}
