/* PineTime sensors: BMA425 accelerometer (step counting) and
   HRS3300 PPG (heart rate). Both share TWIM1 with the touch controller.

   BMA425: raw accel @100 Hz ±2g; steps via peak detection on the
   magnitude delta over a gravity baseline (wrist-mounted). The chip's
   built-in step engine (Bosch config blob) is a later upgrade.
   HRS3300: init sequence follows the wasp-os/InfiniTime-derived register
   values (datasheet); BPM via spectral analysis (DFT peak in the HR
   band) on the raw optical signal. */
#include <stdint.h>
#include <math.h>
#include "board.h"
#include "voidos/hal.h"

int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *dst, unsigned n);
int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);

#define BMA_ADDR 0x18
#define HRS_ADDR 0x44

/* ---- BMA425 ------------------------------------------------------------ */

static int bma_ok;
static int bma_engine;          /* feature engine loaded ok */
static uint32_t steps;
static int16_t ax, ay, az;
static int32_t base;            /* gravity baseline, mg */
static int32_t act_delta;       /* last |mag - base|, for raise-to-wake */
static int armed = 1;
static uint32_t last_step_ms;

#include "bma_cfg.h"

int i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *src, unsigned n);

static void bma_init(void)
{
    uint8_t id = 0;
    i2c_write_reg(BMA_ADDR, 0x7E, 0xB6);      /* soft reset */
    hal_delay_ms(2);
    if (!i2c_read_reg(BMA_ADDR, 0x00, &id, 1) || !id)
        return;                             /* chip id 0x11/0x13/... */
    i2c_write_reg(BMA_ADDR, 0x7C, 0x00);      /* adv power-save off */
    hal_delay_ms(1);

    /* Bosch feature engine: stream the config into internal RAM,
       then start the ASIC so features come alive. Register-level
       sequence mirrors the published sensor driver. */
    i2c_write_reg(BMA_ADDR, 0x59, 0x00);      /* INIT_CTRL: halt loader */
    for (unsigned i = 0; i < BMA_CFG_SIZE; i += 16) {
        i2c_write_reg(BMA_ADDR, 0x5B, (i / 2) & 0x0F);
        i2c_write_reg(BMA_ADDR, 0x5C, (i / 2) >> 4);
        unsigned n = BMA_CFG_SIZE - i < 16 ? BMA_CFG_SIZE - i : 16;
        i2c_write_regs(BMA_ADDR, 0x5E, bma_cfg + i, n);
    }
    i2c_write_reg(BMA_ADDR, 0x59, 0x01);      /* start the ASIC */
    hal_delay_ms(150);
    uint8_t stat = 0;
    i2c_read_reg(BMA_ADDR, 0x2A, &stat, 1);   /* INTERNAL_STAT */
    if ((stat & 0x0F) == 0x01) {              /* ASIC_INITIALIZED */
        /* enable on-chip features: byte 0x3B bit4 = step counter,
           byte 0x40 bit0 = wrist-wear (raise gesture) */
        uint8_t feat[70];
        if (i2c_read_reg(BMA_ADDR, 0x5E, feat, 70)) {
            feat[0x3B] |= 0x10;
            feat[0x40] |= 0x01;
            if (i2c_write_regs(BMA_ADDR, 0x5E, feat, 70)) {
                bma_engine = 1;
                /* latch feature flags until read, and route wrist-wear
                   so a poll can never miss a short pulse */
                i2c_write_reg(BMA_ADDR, 0x55, 0x01);
                i2c_write_reg(BMA_ADDR, 0x56, 0x08);
            }
        }
    }

    i2c_write_reg(BMA_ADDR, 0x40, 0xA8);      /* CIC, avg4, 100Hz */
    i2c_write_reg(BMA_ADDR, 0x41, 0x00);      /* ±2g */
    hal_delay_ms(1);
    i2c_write_reg(BMA_ADDR, 0x7D, 0x04);      /* accel on */
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
    act_delta = delta < 0 ? -delta : delta;
    if (delta < 60)
        armed = 1;

    if (bma_engine) {
        /* the chip's own step algorithm — Bosch feature engine */
        uint8_t s[4];
        if (i2c_read_reg(BMA_ADDR, 0x1E, s, 4))
            steps = (uint32_t)s[0] | ((uint32_t)s[1] << 8) |
                    ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
    } else if (armed && delta > 220 && now - last_step_ms > 300) {
        /* fallback: crude magnitude-delta counter */
        steps++;
        armed = 0;
        last_step_ms = now;
    }
}

/* ---- HRS3300 ----------------------------------------------------------- */

static int hrs_ok;
static int32_t hrs_raw;                  /* last raw sample, for diag */
static int32_t dc, ac_f;                 /* DC estimate, filtered AC */
static uint32_t beats;                   /* analysis windows run (diag) */
static uint32_t last_ibi;                /* diag: last spectrum SNR x10 */
static int bpm_valid, bpm;
static int32_t win_max, prev_win;        /* |ac| max: current/last 2 s window */
static uint32_t win_ms;

/* raw PPG ring buffer for the ECG trace (50 Hz, ~2.5 s of signal) */
#define PPG_N 128
static int16_t ppg[PPG_N];
static int ppg_head;
static int16_t ppg_lo = 32767, ppg_hi = -32768;

/* Spectral BPM: 64 samples @10 Hz -> detrend/differentiate -> EMA
   bandpass -> Hann -> DFT -> dominant bin in the 30-240 BPM region
   with an SNR gate -> rolling average. Same approach as the proven
   open-source PineTime stack; our own implementation of the maths. */
#define HR_N   64
#define HR_SPEC (HR_N / 2)
static uint16_t daq[HR_N];
static int daq_n;
static uint32_t daq_t[HR_N], last_daq_ms;
static float hr_fs = 10.0f;     /* measured sample rate of the window */
static float vre[HR_N], vim[HR_N];
static float spec[HR_SPEC];
static int spec_n, spec_reset = 1;
static float hra[20];
static int hra_i;
static float last_snr;

static void dft64(float *re, float *im)
{
    for (int i = 1, j = 0; i < HR_N; i++) {
        int bit = HR_N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= HR_N; len <<= 1) {
        int half = len >> 1;
        for (int i = 0; i < HR_N; i += len) {
            for (int k = 0; k < half; k++) {
                float w_ang = -6.2831853f * k / len;
                float wr = cosf(w_ang), wi = sinf(w_ang);
                int a = i + k, b = a + half;
                float xr = re[b] * wr - im[b] * wi;
                float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}

static const float hann[HR_N / 2] = {
    0.0f,        0.00248461f, 0.00991376f, 0.0222136f,  0.03926189f,
    0.06088921f, 0.08688061f, 0.11697778f, 0.15088159f, 0.1882551f,
    0.22872687f, 0.27189467f, 0.31732949f, 0.36457977f, 0.41317591f,
    0.46263495f, 0.51246535f, 0.56217185f, 0.61126047f, 0.65924333f,
    0.70564355f, 0.75f,       0.79187184f, 0.83084292f, 0.86652594f,
    0.89856625f, 0.92664544f, 0.95048443f, 0.96984631f, 0.98453864f,
    0.99441541f, 0.99937846f
};

/* returns heart rate in Hz*100, 0 when no usable spectral peak */
static int hr_analyse(void)
{
    for (int i = 0; i < HR_N; i++) {
        vre[i] = (float)daq[i];
        vim[i] = 0;
    }
    /* detrend (linear ramp) then first-difference */
    float off = vre[0];
    float slope = (vre[HR_N - 1] - off) / (HR_N - 1);
    for (int i = 0; i < HR_N; i++)
        vre[i] -= slope * i + off;
    for (int i = 0; i < HR_N - 1; i++)
        vre[i] = vre[i + 1] - vre[i];
    vre[HR_N - 1] = 0;
    /* bandpass: LP ~4 Hz x4 then HP ~0.5 Hz x4 */
    for (int p = 0; p < 4; p++) {
        float e = vre[0];
        for (int i = 0; i < HR_N; i++)
            vre[i] = e = 0.816f * vre[i] + 0.184f * e;
    }
    for (int p = 0; p < 4; p++) {
        float e = vre[0];
        for (int i = 0; i < HR_N; i++) {
            e = 0.268f * vre[i] + 0.732f * e;
            vre[i] -= e;
        }
    }
    for (int i = 0, h = 0; i < HR_N; i++) {
        if (i >= HR_N / 2)
            h--;
        vre[i] *= hann[h];
        if (i < HR_N / 2)
            h++;
    }
    dft64(vre, vim);
    /* gate on THIS frame's spectrum first — otherwise a stale running
       average can keep reporting a peak after the signal is gone */
    float mraw[HR_SPEC];
    float mx = 0, sum = 0;
    for (int k = 0; k < HR_SPEC; k++)
        mraw[k] = sqrtf(vre[k] * vre[k] + vim[k] * vim[k]);
    for (int k = 3; k <= 26; k++) {
        sum += mraw[k];
        if (mraw[k] > mx)
            mx = mraw[k];
    }
    float snr = sum > 0 ? mx / (sum / 24) : 0;
    last_snr = snr;
    /* DC gate relative to the peak: broadband junk lifts bin 0 toward
       the HR-band magnitude, a clean pulse leaves it far below */
    if (snr <= 3.0f || mraw[0] >= mx * 0.05f)
        return 0;
    /* frame passed: merge into the running average */
    if (spec_reset) {
        spec_reset = 0;
        spec_n = 0;
    }
    for (int k = 0; k < HR_SPEC; k++)
        spec[k] = (spec[k] * spec_n + mraw[k]) / (spec_n + 1);
    if (spec_n < 2)
        spec_n++;
    /* recompute peak stats on the smoothed spectrum */
    mx = 0;
    for (int k = 3; k <= 26; k++)
        if (spec[k] > mx)
            mx = spec[k];
    /* exactly one narrow blob above 60% of max */
    float thr = 0.6f * mx;
    int blobs = 0, in = 0, first = 0, last = 0;
    float cent = 0, wsum = 0;
    for (int k = 3; k <= 26; k++) {
        if (spec[k] >= thr) {
            if (!in) {
                in = 1;
                blobs++;
                first = k;
            }
            last = k;
            cent += spec[k] * k;
            wsum += spec[k];
        } else {
            in = 0;
        }
    }
    if (blobs != 1 || wsum <= 0 || (last - first) > 2)
        return 0;
    float hz = (cent / wsum) * hr_fs / HR_N;
    if (hz < 40.0f / 60.0f || hz > 230.0f / 60.0f)
        return 0;
    return (int)(hz * 100 + 0.5f);
}

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
    }
    {
        int32_t a = ac_f < 0 ? -ac_f : ac_f;
        if (a > win_max)
            win_max = a;
    }
    if (hrs < 300 || win_max < 30)
        bpm_valid = 0;

    /* feed the ECG trace: clamp-filtered AC, track display range */
    int16_t v = (int16_t)(ac_f > 32700 ? 32700 : ac_f < -32700 ? -32700 : ac_f);
    ppg[ppg_head] = v;
    ppg_head = (ppg_head + 1) % PPG_N;
    if (v < ppg_lo) ppg_lo = v;
    if (v > ppg_hi) ppg_hi = v;

    /* spectral DAQ: one sample per >=100 ms of wall time. The main loop
       can run far slower than 20 Hz while the display renders, so the
       window carries real timestamps and the true rate is measured */
    if (daq_n > 0 || last_daq_ms) {
        if (now - last_daq_ms < 100)
            return;
    }
    last_daq_ms = now;
    daq_t[daq_n] = now;
    daq[daq_n] = (uint16_t)hrs;
    if (daq_n < HR_N - 1) {
        daq_n++;
        return;
    }
    uint32_t span = daq_t[HR_N - 1] - daq_t[0];
    hr_fs = span ? (HR_N - 1) * 1000.0f / span : 10.0f;
    if (hr_fs < 5.0f || hr_fs > 12.0f) {   /* gap in sampling: restart */
        daq_n = 0;
        return;
    }
    int hz100 = hr_analyse();
    for (int i = 0; i < HR_N - 5; i++) { /* 0.5 s slide between runs */
        daq[i] = daq[i + 5];
        daq_t[i] = daq_t[i + 5];
    }
    daq_n = HR_N - 5;
    beats++;
    last_ibi = (uint32_t)(last_snr * 10);
    if (hz100 == 0) {
        spec_reset = 1;
        if (bpm_valid) {                 /* signal lost: drop, re-acquire */
            for (int i = 0; i < 20; i++)
                hra[i] = 0;
            bpm = 0;
            bpm_valid = 0;
            daq_n = 0;
        }
        return;
    }
    hra[hra_i++ % 20] = hz100 / 100.0f;
    float sum = 0;
    int cnt = 0;
    for (int i = 0; i < 20; i++)
        if (hra[i] > 0) {
            sum += hra[i];
            cnt++;
        }
    if (cnt >= 3) {
        bpm = (int)(60.0f * sum / cnt + 0.5f);
        bpm_valid = 1;
    }
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
int sensors_bma_activity(void) { return bma_ok ? (int)act_delta : 0; }

/* wrist-raise gesture from the feature engine: INT_STAT_1 bit3,
   clears on read — 1 when the engine flagged a raise since last poll */
int sensors_wrist_raise(void)
{
    if (!bma_engine)
        return 0;
    /* feature interrupts (step/activity/wrist-wear) live in INT_STATUS_0
       (0x1C); INT_STATUS_1 (0x1D) carries data-ready/FIFO flags */
    uint8_t v = 0;
    if (!i2c_read_reg(BMA_ADDR, 0x1C, &v, 1))
        return 0;
    return (v & 0x08) ? 1 : 0;
}
int sensors_bma_engine(void) { return bma_engine; }
int sensors_hrs_fs10(void) { return (int)(hr_fs * 10.0f); }
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

/* ECG trace data: sample i (0..PPG_N-1) oldest→newest, normalised -1..1.
   Real signal whenever there's measurable amplitude — a missing BPM
   lock mustn't hide live data behind the fake waveform. */
int sensors_ppg(int i, int *out_y100)
{
    if (!hrs_ok)
        return 0;
    int idx = (ppg_head + i) % PPG_N;
    int32_t range = ppg_hi - ppg_lo;
    if (range < 40)
        return 0;
    *out_y100 = (int)((ppg[idx] - ppg_lo) * 200 / range - 100);
    return 1;
}
