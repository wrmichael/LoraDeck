// Console log in flash (see conlog.h). Log calls only copy their text into a RAM ring; a task appends
// it to the file every few seconds, so logging never waits for the flash and the file system's own
// messages cannot loop.
#include "conlog.h"
#include "clock.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char* DIR_ = "/data/logs";
static const char* CUR = "/data/logs/console.txt";
static const char* OLD = "/data/logs/console.old.txt";
static const long MAX_FILE = 128 * 1024;

static const size_t RING = 8 * 1024;       // no PSRAM on the V3: keep the RAM buffer small
static char s_ring[RING];
static size_t s_head, s_tail;
static unsigned s_dropped;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev;
static SemaphoreHandle_t s_file;           // file access: writer task vs. web page

static int teeVprintf(const char* fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int r = s_prev ? s_prev(fmt, ap) : vprintf(fmt, ap);
    char line[200];
    int n = vsnprintf(line, sizeof line, fmt, ap2);
    va_end(ap2);
    if (n > 0) {
        if (n > (int)sizeof line - 1) n = sizeof line - 1;
        portENTER_CRITICAL_SAFE(&s_mux);
        if (s_head - s_tail + n <= RING) {
            for (int i = 0; i < n; i++) s_ring[(s_head + i) % RING] = line[i];
            s_head += n;
        } else {
            s_dropped++;
        }
        portEXIT_CRITICAL_SAFE(&s_mux);
    }
    return r;
}

static void timeMark(FILE* f, const char* what) {
    if (!clk::utcValid()) { fprintf(f, "---- %s (clock not set) ----\n", what); return; }
    time_t t = (time_t)(clk::utcMs() / 1000);
    struct tm tm;
    gmtime_r(&t, &tm);
    char b[40];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S UTC", &tm);
    fprintf(f, "---- %s %s ----\n", what, b);
}

static void writerTask(void*) {
    static char chunk[1024];
    bool first = true, hadClock = false;
    int64_t lastMark = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(first ? 200 : 3000));
        xSemaphoreTake(s_file, portMAX_DELAY);
        FILE* f = fopen(CUR, "a");
        if (f) {
            bool clock = clk::utcValid();
            int64_t mono = clk::monoMs();
            if (first) timeMark(f, "LoRaDeck start");
            else if (clock && !hadClock) timeMark(f, "clock set");
            else if (mono - lastMark >= 60000) timeMark(f, "time");
            if (first || mono - lastMark >= 60000 || (clock && !hadClock)) lastMark = mono;
            first = false;
            hadClock = clock;
            for (;;) {
                size_t n = 0;
                unsigned dropped;
                portENTER_CRITICAL(&s_mux);
                while (s_tail != s_head && n < sizeof chunk) chunk[n++] = s_ring[s_tail++ % RING];
                dropped = s_dropped;
                s_dropped = 0;
                portEXIT_CRITICAL(&s_mux);
                if (dropped) fprintf(f, "[console log: %u messages lost, buffer full]\n", dropped);
                if (!n) break;
                fwrite(chunk, 1, n, f);
            }
            long size = ftell(f);
            fclose(f);
            if (size > MAX_FILE) {                              // oldest lines go first
                remove(OLD);
                rename(CUR, OLD);
            }
        }
        xSemaphoreGive(s_file);
    }
}

void conlog_start() {
    if (s_file) return;
    mkdir(DIR_, 0775);
    s_file = xSemaphoreCreateMutex();
    s_prev = esp_log_set_vprintf(teeVprintf);
    xTaskCreate(writerTask, "conlog", 3072, nullptr, 1, nullptr);
}

static void readAll(const char* path, std::string& out) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
}

std::string conlog_read(size_t tailBytes) {
    std::string out;
    if (!s_file) return out;
    xSemaphoreTake(s_file, portMAX_DELAY);
    if (tailBytes == 0) {
        readAll(OLD, out);
        readAll(CUR, out);
    } else {
        FILE* f = fopen(CUR, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            long from = size > (long)tailBytes ? size - (long)tailBytes : 0;
            fseek(f, from, SEEK_SET);
            char buf[512];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
            fclose(f);
            if (from > 0) {                                     // start at a whole line
                size_t nl = out.find('\n');
                if (nl != std::string::npos) out.erase(0, nl + 1);
            }
        }
    }
    xSemaphoreGive(s_file);
    return out;
}

void conlog_clear() {
    if (!s_file) return;
    xSemaphoreTake(s_file, portMAX_DELAY);
    remove(OLD);
    remove(CUR);
    xSemaphoreGive(s_file);
}
