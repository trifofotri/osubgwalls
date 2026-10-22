#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <obwlog.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <unistd.h>
#include <zip.h>

#define UA "Mozilla/5.0 (X11; Linux x86_64; rv:157.0) Gecko/20100101 Firefox/157.0"
#define MIN_ID 10000
#define MAX_ID 2650000

typedef struct libcurl_cb_str {
    char *response;
    size_t size;
} libcurl_cb_memory;

static int is_saved(const cJSON *arr, int id) {
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (cJSON_IsNumber(it) && it->valueint == id) return 1;
    }
    return 0;
}

static int cover_exists(CURL *curl, int id) {
    char url[128];
    snprintf(url, sizeof(url), "https://assets.ppy.sh/beatmaps/%d/covers/card.jpg", id);

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UA);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    if (curl_easy_perform(curl) != CURLE_OK) return 0;

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    return code == 200;
}

static size_t libcurl_wdcb(char *data, size_t size, size_t nmemb, void *clientp) {
    size_t realsize = size * nmemb;
    struct libcurl_cb_str *mem = clientp;

    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->response = ptr;
    memcpy(mem->response + mem->size, data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = '\0';

    return realsize;
}

size_t libcurl_download_wdcb(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

static int get_download_url(CURL *curl, int id, const char *session, char *out, size_t outsz, long *http_code) {
    char page_url[128], referer[128], cookie[4096];
    snprintf(page_url, sizeof(page_url), "https://osu.ppy.sh/beatmapsets/%d/download", id);
    snprintf(referer, sizeof(referer), "https://osu.ppy.sh/beatmapsets/%d", id);
    snprintf(cookie, sizeof(cookie), "osu_session=%s", session);

    struct libcurl_cb_str chunk = { 0 };

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, page_url);
    curl_easy_setopt(curl, CURLOPT_REFERER, referer);
    curl_easy_setopt(curl, CURLOPT_COOKIE, cookie);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UA);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, libcurl_wdcb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    if (curl_easy_perform(curl) != CURLE_OK || !chunk.response) {
        free(chunk.response);
        return 0;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code);

    int ok = 0;
    char *start = strstr(chunk.response, "url='");
    if (start) {
        start += 5;
        char *end = strchr(start, '\'');
        if (end) {
            size_t o = 0;
            for (char *p = start; p < end && o + 1 < outsz; ) {
                if (strncmp(p, "&amp;", 5) == 0) { out[o++] = '&'; p += 5; }
                else out[o++] = *p++;
            }
            out[o] = '\0';
            ok = 1;
        }
    }

    free(chunk.response);
    return ok;
}

static const char *bar_colors[] = {
    "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[34m", "\x1b[35m", "\x1b[36m"
};

static const char *bar_color = "\x1b[36m";

static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (dltotal <= 0) return 0;

    static const char *shades[] = {" ", "⣀", "⣄", "⣤", "⣦", "⣶", "⣷", "⣿"};
    const int width = 31;

    float p = (float)dlnow / (float)dltotal;
    float filled = p * width;

    int whole = (int)filled;
    int partial = (int)((filled - whole) * 7);

    printf("\r\033[2K%s[", bar_color);

    for (int i = 0; i < whole; i++) {
        printf("⣿");
    }

    if (whole < width) {
        printf("%s", shades[partial]);
    }

    for (int i = whole + 1; i < width; i++) {
        printf("⠀");
    }

    printf("] " ANSI_COLOR_RESET " %3d%% (%.1f/%.1f MB)", (int)(p * 100), dlnow / 1048576.0, dltotal / 1048576.0);

    fflush(stdout);
    return 0;
}
static int download_file(CURL *curl, const char *url, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror("fopen"); return 0; }

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, libcurl_download_wdcb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);     
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);

    bar_color = bar_colors[rand() % (sizeof(bar_colors) / sizeof(bar_colors[0]))];

    CURLcode res = curl_easy_perform(curl);
    printf("\n");
    fclose(fp);

    if (res != CURLE_OK) {
        fprintf(stderr, "Download failed: %s\n", curl_easy_strerror(res));
        remove(path);
        return 0;
    }
    return 1;
}

char *randstring(size_t length) {
    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";        
    char *randomString = NULL;

    if (length) {
        randomString = malloc(sizeof(char) * (length +1));

        if (randomString) {            
            for (int n = 0;n < length;n++) {            
                int key = rand() % (int)(sizeof(charset) -1);
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }

    return randomString;
}

static char *read_zip_entry(zip_t *za, zip_int64_t idx) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat_index(za, idx, 0, &st) != 0 || !(st.valid & ZIP_STAT_SIZE)) return NULL;

    zip_file_t *zf = zip_fopen_index(za, idx, 0);
    if (!zf) return NULL;

    char *buf = malloc(st.size + 1);
    if (!buf) { zip_fclose(zf); return NULL; }

    zip_int64_t r = zip_fread(zf, buf, st.size);
    zip_fclose(zf);
    if (r < 0) { free(buf); return NULL; }

    buf[r] = '\0';
    return buf;
}

static int parse_background(char *text, char *out, size_t outsz) {
    int in_events = 0;
    char *saveptr;

    for (char *line = strtok_r(text, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[len - 1] = '\0';

        if (line[0] == '[') {
            in_events = !strncmp(line, "[Events]", 8);
            continue;
        }
        if (!in_events) continue;

        if (strncmp(line, "0,0,", 4) != 0) continue;

        char *q1 = strchr(line, '"');
        if (!q1) continue;
        char *q2 = strchr(q1 + 1, '"');
        if (!q2) continue;

        *q2 = '\0';
        snprintf(out, outsz, "%s", q1 + 1);
        return 1;
    }
    return 0;
}

static int extract_images(const char *osz_path, const char *out_dir, int beatmap_id) {
    int err = 0;
    zip_t *za = zip_open(osz_path, ZIP_RDONLY, &err);
    if (!za) {
        fprintf(stderr, "zip_open failed (code %d)\n", err);
        return -1;
    }

    mkdir(out_dir, 0755);

    char bgs[16][256];
    int nbg = 0;
    zip_int64_t n = zip_get_num_entries(za, 0);

    for (zip_int64_t i = 0; i < n && nbg < 16; i++) {
        const char *name = zip_get_name(za, i, 0);
        if (!name) continue;

        const char *dot = strrchr(name, '.');
        if (!dot || strcasecmp(dot, ".osu") != 0) continue;

        char *text = read_zip_entry(za, i);
        if (!text) continue;

        char bg[256];
        if (parse_background(text, bg, sizeof(bg))) {
            for (char *p = bg; *p; p++) if (*p == '\\') *p = '/';

            int dup = 0;
            for (int k = 0; k < nbg; k++) {
                if (!strcasecmp(bgs[k], bg)) { dup = 1; break; }
            }
            if (!dup) snprintf(bgs[nbg++], sizeof(bgs[0]), "%s", bg);
        }
        free(text);
    }

    if (nbg == 0) {
        printf("  no background found in any difficulty.\n");
    }

    int count = 0;
    for (int b = 0; b < nbg; b++) {
        zip_int64_t idx = zip_name_locate(za, bgs[b], ZIP_FL_NOCASE);
        if (idx < 0) {
            printf("  background \"%s\" is listed but missing from the archive\n", bgs[b]);
            continue;
        }

        const char *base = bgs[b];
        for (const char *p = bgs[b]; *p; p++) {
            if (*p == '/') base = p + 1;
        }
        if (!*base) continue;

        zip_file_t *zf = zip_fopen_index(za, idx, 0);
        if (!zf) continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%d_%s", out_dir, beatmap_id, base);

        FILE *fp = fopen(path, "wb");
        if (!fp) {
            perror("fopen");
            zip_fclose(zf);
            continue;
        }

        char buf[8192];
        zip_int64_t r;
        while ((r = zip_fread(zf, buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, (size_t)r, fp);
        }

        fclose(fp);
        zip_fclose(zf);

        count++;
    }

    zip_close(za);
    return count;
}

static void wait_with_countdown(int secs, const char *why) {
    for (int s = secs; s > 0; s--) {
        printf("\r\033[2K" ANSI_COLOR_YELLOW "%s Retrying in %ds..." ANSI_COLOR_RESET, why, s);
        fflush(stdout);
        sleep(1);
    }
    printf("\r\033[2K");
}

int main(int argc, char** argv) {
    printf("----------------------------------------------\n");
    printf(ANSI_COLOR_BLUE "osubgwalls - Download osu beatmap backgrounds!\n" ANSI_COLOR_RESET);
    printf("----------------------------------------------\n\n");

    int count = 1;
    if (argc > 1) {
        count = atoi(argv[1]);
        if (count < 1) {
            fprintf(stderr, "Usage: %s [count]\n", argv[0]);
            return 1;
        }
    }

    const char *session = getenv("OSU_SESSION");
    if (!session) {
        fprintf(stderr, "Set OSU_SESSION!!\n");
        return 1;
    }

    char cookie[4096];
    snprintf(cookie, sizeof(cookie), "osu_session=%s", session);

    srand(time(NULL)); // set the random seejd
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize curl.\n");
        return 1;
    }

    FILE *saved_fp = fopen("saved.json", "r");
    cJSON *already_saved_json = NULL;

    if (saved_fp != NULL) {
        if (fseek(saved_fp, 0, SEEK_END) == 0) {
            long file_size = ftell(saved_fp);

            if (file_size >= 0 && fseek(saved_fp, 0, SEEK_SET) == 0) {
                char *buffer = malloc((size_t)file_size + 1);

                if (buffer != NULL) {
                    size_t bytes_read = fread(buffer, 1, (size_t)file_size, saved_fp);
                    buffer[bytes_read] = '\0';

                    already_saved_json = cJSON_Parse(buffer);
                    free(buffer);
                }
            }
        }

        fclose(saved_fp);
    }

    if (!cJSON_IsArray(already_saved_json)) {
        cJSON_Delete(already_saved_json);
        already_saved_json = cJSON_CreateArray();

        if (already_saved_json == NULL) {
            fprintf(stderr, "Failed to create saved-beatmaps array.\n");
            curl_easy_cleanup(curl);
            return 1;
        }
    }

    printf(ANSI_COLOR_MAGENTA "%d beatmap backgrounds already saved!\n" ANSI_COLOR_RESET, cJSON_GetArraySize(already_saved_json));
    int total_images = 0;

    for (int n = 1; n <= count; n++) {
        printf("\n[%d/%d]\n", n, count);
        int picked_beatmap_id = 0;

        int tries = 0;
        while (!picked_beatmap_id) {
            int id = MIN_ID + rand() % (MAX_ID - MIN_ID);
            if (is_saved(already_saved_json, id)) continue;

            printf("\r\033[2K" ANSI_COLOR_YELLOW "Checking beatmap %d... (%d tried)" ANSI_COLOR_RESET, id, ++tries);
            fflush(stdout);

            if (!cover_exists(curl, id)) continue;
            picked_beatmap_id = id;
        }

        printf("\r\033[2K" ANSI_COLOR_BLUE "[%d] https://osu.ppy.sh/beatmapsets/%d chosen! Downloading beatmap...\n" ANSI_COLOR_RESET, picked_beatmap_id, picked_beatmap_id);
        curl_easy_reset(curl);

        int fails_in_a_row = 0;
        char real_url[1024];
        int got_url = 0;
        long http = 0;

        for (int attempt = 0; attempt < 4 && !got_url; attempt++) {
            if (attempt > 0) {
                char why[96];
                snprintf(why, sizeof(why), "No download link (HTTP %ld), maybe rate limited.", http);
                wait_with_countdown(30 * attempt, why);
            }
            got_url = get_download_url(curl, picked_beatmap_id, session, real_url, sizeof(real_url), &http);
            if (!got_url && (http == 404 || http == 410)) break;   // gone/deleted idk
        }

        if (!got_url) {
            if (++fails_in_a_row >= 2) {
                fprintf(stderr, "Failed twice in a row (last HTTP %ld). Session expired or blocked, stopping.\n", http);
                break;
            }
            printf(ANSI_COLOR_YELLOW "Skipping %d, it won't give a download link.\n" ANSI_COLOR_RESET, picked_beatmap_id);
            n--; 
            continue;
        }
        fails_in_a_row = 0;

        char out_name[64];
        snprintf(out_name, sizeof(out_name), "%d.osz", picked_beatmap_id);

        if (!download_file(curl, real_url, out_name)) continue;

        int extracted = extract_images(out_name, "backgrounds", picked_beatmap_id);
        remove(out_name);
        if (extracted < 0) continue;

        printf(ANSI_COLOR_GREEN "Extracted %d image%s.\n" ANSI_COLOR_RESET, extracted, extracted == 1 ? "" : "s");
        total_images += extracted;

        cJSON_AddItemToArray(already_saved_json, cJSON_CreateNumber(picked_beatmap_id));
        char *already_saved_json_str = cJSON_Print(already_saved_json);

        saved_fp = fopen("saved.json", "w");
        if (saved_fp != NULL) {
            fputs(already_saved_json_str, saved_fp);
            fclose(saved_fp);
        }
        free(already_saved_json_str);

        if (n < count) sleep(1);
    }

    printf(ANSI_COLOR_MAGENTA "\nDone! %d image(s) total.\n" ANSI_COLOR_RESET, total_images);

    return 0;
}