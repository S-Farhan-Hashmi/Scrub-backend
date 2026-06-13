#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float latitude;
    float longitude;
    bool  valid;
} GPS_Data_t;

/* Parse a $GPRMC or $GNRMC NMEA sentence (null-terminated, no \r\n).
 * Returns true if the sentence is valid and position was extracted. */
bool GPS_ParseRMC(const char *sentence, GPS_Data_t *out);

#endif /* GPS_NMEA_H */
