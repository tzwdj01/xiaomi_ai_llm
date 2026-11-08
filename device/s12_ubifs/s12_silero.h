/* Experimental 16 kHz Silero v5 detector. Not connected to device audio. */
#ifndef S12_SILERO_H
#define S12_SILERO_H
#include <stddef.h>
struct s12_silero;
struct s12_silero *s12_silero_open(const char *runtime, const char *model);
/* A stream is limited to 20 seconds. Any input/inference/queue failure is
 * sticky: return 0 and require the caller to abort and destroy the detector. */
int s12_silero_accept(struct s12_silero *v, const float *samples, size_t count);
int s12_silero_detected(const struct s12_silero *v); /* -1 error, else 0/1 */
int s12_silero_pop_segment(struct s12_silero *v);    /* -1 error, else 0/1 */
int s12_silero_reset(struct s12_silero *v);          /* starts a clean stream */
void s12_silero_close(struct s12_silero *v);
#endif
