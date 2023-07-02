/* mobilews rg15.c
 * routines for the RG15 rain sensor */

#include <string.h>
#include <esp_log.h>
#include "rg15.h"
#include "wk2132.h"

#define RG15SERPORT 0

void rg15_init(void)
{
    /* The RG15 communicates at 9600 baud by default unless we
     * reconfigure it, and we don't plan to.
     * It's connected on wk2132 port 0 (define RG15SERPORT). */
    wk2132_serialportinit(RG15SERPORT, 9600);
    /* Tell the rainsensor we want polling mode, a.k.a. "shut up until you're spoken to".
     * Also, use high res mode and metrical output, disable 
     * tipping-bucket-output, and reset counters. */
    wk2132_write_serial(RG15SERPORT, "P\nH\nM\nY\nO\n", 10);
}

void rg15_requestread(void)
{
    /* Request a new reading.
     * Since we're only interested in changes, "A" should fit us well,
     * but we could also get summed up data with "R". */
    wk2132_write_serial(RG15SERPORT, "A\n", 2);
    /* Flush output */
    wk2132_flush(RG15SERPORT);
}

float rg15_readraincount(void)
{
    char rcvdata[128];
    int length = 0;
    float res = -99999.9;
    if ((length = wk2132_get_available_to_read(RG15SERPORT)) < 1/* FIXME 10 */) {
      ESP_LOGW("rg15.c", "No or not enough data available on serial port.");
      return -99999.9;
    }
    length = wk2132_read_serial(RG15SERPORT, rcvdata, ((length > 100) ? 100 : length));
    if (length > 0) {
      char * stsp; char * spp;
      rcvdata[length] = 0;
      ESP_LOGI("rg15.c", "Serial received %d bytes: %s", length, rcvdata);
      /* Split into lines */
      spp = strtok_r(&rcvdata[0], "\n", &stsp);
      do {
        ESP_LOGI("rg15.c", "Parsing: %s", spp);
        /* Attempt to parse the line. There are a few things we can safely ignore. */
        /* Lines starting with ";" are comments. */
        if (strncmp(spp, ";", 1) == 0) continue;
        /* "h" is acknowledgement of the "H" command that sets high resolution. */
        if ((strncmp(spp, "h", 1) == 0) && (strlen(spp) <= 3)) continue;
        /* "m" is acknowledgement of the "M" command that sets metric units. */
        if ((strncmp(spp, "m", 1) == 0) && (strlen(spp) <= 3)) continue;
        /* "y" is acknowledgement of the "Y" command that disables tipping bucket output. */
        if ((strncmp(spp, "m", 1) == 0) && (strlen(spp) <= 3)) continue;
        if (strncmp(spp, "Acc ", 4) == 0) {
          /* Acc  0.00 mm\r\n */
          char rcv1[128];
          if (sscanf(spp+4, "%f %s", &res, rcv1) < 2) {
            ESP_LOGW("rg15.c", "...failed to parse serial input (1).");
            return -99999.9;
          }
          if (strncmp(rcv1, "mm", 2) != 0) {
            ESP_LOGW("rg15.c", "...failed to parse serial input (2).");
            return -99999.9;
          }
          ESP_LOGI("rg15.c", "Successfully parsed raingauge value: %.3f", res);
        }
      } while ((spp = strtok_r(NULL, "\n", &stsp)) != NULL);
    }
    return res;
}

