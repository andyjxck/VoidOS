/* PineTime sensors: BMA425 accelerometer (step counting) and
   HRS3300 PPG (heart rate). Both share TWIM1 with the touch controller.

   BMA425: raw accel @100 Hz ±2g; steps via peak detection on the
   magnitude delta over a gravity baseline (wrist-mounted). The chip's
   built-in step engine (Bosch config blob) is a later upgrade.
   HRS3300: init sequence follows the wasp-os/InfiniTime-derived register
   values (datasheet); BPM via adaptive-threshold peak detection on the
   AC component of the optical signal. */
#include <stdint.h>
#include "board.h"
#include "voidos/hal.h"

int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *dst, unsigned n);
int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);

#define BMA_ADDR 0x18
#define HRS_ADDR 0x44

/* ---- BMA425 ------------------------------------------------------------ */

static int bma_ok;
static uint32_t steps;
static int16_t ax, ay, az;
static int32_t base;            /* gravity baseline, mg */
static int armed = 1;
static uint32_t last_step_ms;

static void bma_init(void)
{
    uint8_t id = 0;
    i2c_write_reg(BMA_ADDR, 0x7E, 0xB6);      /* soft reset */
    hal_delay_ms(2);
    if (!i2c_read_reg(BMA_ADDR, 0x00, &id, 1) || !id)
        return;                             /* chip id 0x11/0x13/... */
    i2c_write_reg(BMA_ADDR, 0x7C, 0x00);      /* PWR_CONF: adv power-save off
                                               so config writes stick */
    hal_delay_ms(1);
    i2c_write_reg(BMA_ADDR, 0x40, 0xA8);      /* ACC_CONF: CIC, avg4, 100Hz */
    i2c_write_reg(BMA_ADDR, 0x41, 0x00);      /* ACC_RANGE: ±2g */
    hal_delay_ms(1);
    i2c_write_reg(BMA_ADDR, 0x7D, 0x04);      /* PWR_CTRL: accel on */
    hal_delay_ms(50);
    bma_ok = 1;
}

static int32_t isqrt(int32_t v)
{
    int32_t r = 0;
    for (int32_t bit = 0x40000000; bit; bit >>= 2) {
        int32_t t = r + bit;
        r >>= 1;
        if (v >= t) {
            v -= t;
            r += bit;
        }
    }
    return r;
}

static void bma_sample(uint32_t now)
{
    uint8_t d[6];
    if (!i2c_read_reg(BMA_ADDR, 0x12, d, 6))
        return;
    int16_t x = (int16_t)(((d[1] << 8) | d[0]) >> 4);
    int16_t y = (int16_t)(((d[3] << 8) | d[2]) >> 4);
    int16_t z = (int16_t)(((d[5] << 8) | d[4]) >> 4);
    if (x > 2047) x -= 4096;      /* sign-extend 12-bit */
    if (y > 2047) y -= 4096;
    if (z > 2047) z -= 4096;
    /* mounted rotated: swap X/Y to match the watch axes */
    ax = y; ay = x; az = z;

    int32_t mag = isqrt((int32_t)ax * ax + (int32_t)ay * ay +
                        (int32_t)az * az);
    if (!base)
        base = mag;
    base += (mag - base) >> 5;
    int32_t delta = mag - base;
    if (delta < 60)
        armed = 1;
    if (armed && delta > 220 && now - last_step_ms > 300) {
        steps++;
        armed = 0;
        last_step_ms = now;
    }
}

/* ---- HRS3300 ----------------------------------------------------------- */

static int hrs_ok;
static int32_t hrs_raw;                  /* last raw sample, for diag */
static int32_t dc, ac_f;                 /* DC estimate, filtered AC */
static int32_t thr_env;                  /* threshold anchored to window */
static int beat_armed;
static uint32_t last_peak_ms;
static uint32_t beats;                   /* total detected peaks (diag) */
static uint32_t last_ibi;                /* last beat-to-beat gap (diag) */
static uint32_t ibi[4];                  /* inter-beat intervals, ms */
static int ibi_n;
static int bpm_valid, bpm;
static int32_t win_max, prev_win;        /* |ac| max: current/last 2 s window */
static uint32_t win_ms;

/* raw PPG ring buffer for the ECG trace (50 Hz, ~2.5 s of signal) */
#define PPG_N 128
static int16_t ppg[PPG_N];
static int ppg_head;
static int16_t ppg_lo = 32767, ppg_hi = -32768;

static void hrs_init(void)
{
    uint8_t id;
    if (!i2c_read_reg(HRS_ADDR, 0x00, &id, 1))
        return;
    /* disabled, 50 ms conversion period, LED 12.5 mA */
    i2c_write_reg(HRS_ADDR, 0x01, 0x50);
    /* 0xF low nibble -> clean steady LED during conversion */
    i2c_write_reg(HRS_ADDR, 0x0C, 0x2f);
    i2c_write_reg(HRS_ADDR, 0x16, 0x77);  /* HRS+ALS 15-bit */
    i2c_write_reg(HRS_ADDR, 0x17, 0x00);  /* gain 1x */
    hal_delay_ms(100);
    i2c_write_reg(HRS_ADDR, 0x01, 0x50 | 0x80);  /* enable */
    i2c_write_reg(HRS_ADDR, 0x0C, 0x2f);
    hrs_ok = 1;
}

static void hrs_sample(uint32_t now)
{
    uint8_t d[8];
    if (!i2c_read_reg(HRS_ADDR, 0x08, d, 8))
        return;
    int32_t hrs = ((int32_t)d[1] << 8) | ((int32_t)(d[2] & 0x0f) << 4) |
                  (d[7] & 0x0f);
    hrs_raw = hrs;

    /* DC removal + smoothing */
    dc += (hrs - dc) >> 7;
    int32_t ac = hrs - dc;
    ac_f += (ac - ac_f) >> 2;

    /* contact check: raw near zero or dead-flat AC = off wrist */
    if (now - win_ms >= 2000) {
        prev_win = win_max;
        win_max = 0;
        ppg_lo = 32767;
        ppg_hi = -32768;
        win_ms = now;
        /* re-anchor the beat threshold to the measured amplitude each
           window — a one-off spike can no longer disable detection.
           prev_win/2: catches the systolic peak but not the smaller
           dicrotic notch (that was doubling the apparent rate). */
        thr_env = (prev_win >> 1) > 40 ? (prev_win >> 1) : 40;
    }
    {
        int32_t a = ac_f < 0 ? -ac_f : ac_f;
        if (a > win_max)
            win_max = a;
    }
    if (hrs < 300 || win_max < 30) {
        bpm_valid = 0;
        ibi_n = 0;
    }

    int32_t thr = thr_env;

    /* a beat seen >3 s ago is stale — without this a lost signal leaves
       the last measured rate on screen indefinitely */
    if (bpm_valid && now - last_peak_ms > 3000) {
        bpm_valid = 0;
        ibi_n = 0;
    }

    /* arm whenever the signal crosses below the DC line — noise spikes
       can hold ac_f high, but a real pulse always dips back through 0 */
    if (ac_f < 0)
        beat_armed = 1;
    if (beat_armed && ac_f > thr && now - last_peak_ms > 300) {
        uint32_t i = now - last_peak_ms;
        last_ibi = i;
        if (i > 300 && i < 2000 && last_peak_ms) {
            /* reject outliers once the window is established: a single
               missed or double-counted beat used to drag the median to
               half or double the true rate for several seconds */
            int ok = 1;
            if (ibi_n >= 3) {
                uint32_t s0[4];
                for (int k = 0; k < ibi_n; k++) s0[k] = ibi[k];
                for (int a = 0; a < ibi_n - 1; a++)
                    for (int b = a + 1; b < ibi_n; b++)
                        if (s0[b] < s0[a]) {
                            uint32_t t = s0[a]; s0[a] = s0[b]; s0[b] = t;
                        }
                uint32_t med0 = s0[ibi_n / 2];
                if (i < med0 * 3 / 4 || i > med0 * 4 / 3)
                    ok = 0;
            }
            if (!ok) {
                last_peak_ms = now;
                beat_armed = 0;
                beats++;
            } else {
            if (ibi_n < 4)
                ibi[ibi_n++] = i;
            else {
                for (int k = 0; k < 3; k++)
                    ibi[k] = ibi[k + 1];
                ibi[3] = i;
            }
            /* median-ish: sort the small window */
            uint32_t s[4];
            for (int k = 0; k < ibi_n; k++) s[k] = ibi[k];
            for (int a = 0; a < ibi_n - 1; a++)
                for (int b = a + 1; b < ibi_n; b++)
                    if (s[b] < s[a]) {
                        uint32_t t = s[a]; s[a] = s[b]; s[b] = t;
                    }
            uint32_t med = s[ibi_n / 2];
            bpm = (int)(60000u / med);
            if (bpm >= 35 && bpm <= 200)
                bpm_valid = 1;
            }
        }
        last_peak_ms = now;
        beat_armed = 0;
        beats++;
    }

    /* feed the ECG trace: clamp-filtered AC, track display range */
    int16_t v = (int16_t)(ac_f > 32700 ? 32700 : ac_f < -32700 ? -32700 : ac_f);
    ppg[ppg_head] = v;
    ppg_head = (ppg_head + 1) % PPG_N;
    if (v < ppg_lo) ppg_lo = v;
    if (v > ppg_hi) ppg_hi = v;
}

/* ---- public -------------------------------------------------------------- */

void sensors_init(void)
{
    bma_init();
    hrs_init();
}

void sensors_tick(uint32_t now)
{
    static uint32_t last_bma, last_hrs;
    if (bma_ok && now - last_bma >= 20) {   /* 50 Hz accel */
        last_bma = now;
        bma_sample(now);
    }
    if (hrs_ok && now - last_hrs >= 50) {   /* 20 Hz PPG */
        last_hrs = now;
        hrs_sample(now);
    }
}

int sensors_bma_ok(void)  { return bma_ok; }
int sensors_bma_z(void)   { return bma_ok ? az : 0; }
int sensors_hrs_ok(void)  { return hrs_ok; }
int sensors_hrs_raw(void)   { return (int)hrs_raw; }
int sensors_hrs_ac(void)    { return (int)win_max; }
int sensors_hrs_beats(void) { return (int)beats; }
int sensors_hrs_ibi(void)   { return (int)last_ibi; }
int sensors_steps(void)   { return bma_ok ? (int)steps : -1; }
int sensors_bpm(void)
{
    if (!hrs_ok) return -1;   /* no sensor */
    return bpm_valid ? bpm : 0;   /* 0 = measuring / poor contact */
}

/* ECG trace data: sample i (0..PPG_N-1) oldest→newest, normalised -1..1 */
int sensors_ppg(int i, int *out_y100)
{
    if (!hrs_ok || !bpm_valid)
        return 0;
    int idx = (ppg_head + i) % PPG_N;
    int32_t range = ppg_hi - ppg_lo;
    if (range < 20)
        return 0;
    *out_y100 = (int)((ppg[idx] - ppg_lo) * 200 / range - 100);
    return 1;
}
