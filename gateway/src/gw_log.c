#include "gw_log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef GW_WITH_SQLITE
#include <sqlite3.h>

static sqlite3 *g_db;

static char *hexdup(const uint8_t *data, uint16_t len)
{
    static const char hex[] = "0123456789ABCDEF";
    char *out;
    uint16_t i;

    out = malloc((size_t)len * 2U + 1U);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        out[i * 2U] = hex[data[i] >> 4];
        out[i * 2U + 1U] = hex[data[i] & 0x0FU];
    }
    out[(size_t)len * 2U] = '\0';
    return out;
}

static void iso_time(char *buf, size_t cap)
{
    time_t now = time(NULL);
    struct tm tm_now;

    if (localtime_r(&now, &tm_now) == NULL) {
        strncpy(buf, "1970-01-01T00:00:00", cap - 1U);
        buf[cap - 1U] = '\0';
        return;
    }
    strftime(buf, cap, "%Y-%m-%dT%H:%M:%S", &tm_now);
}

void gw_log_init(const char *path)
{
    const char *db_path = path;
    char *err = NULL;

    if (g_db) {
        return;
    }
    if (!db_path) {
        db_path = getenv("GW_DB");
    }
    if (!db_path || !*db_path) {
        db_path = "gateway.db";
    }

    if (sqlite3_open(db_path, &g_db) != SQLITE_OK) {
        if (g_db) {
            sqlite3_close(g_db);
            g_db = NULL;
        }
        return;
    }

    sqlite3_busy_timeout(g_db, 1000);
    if (sqlite3_exec(g_db,
            "CREATE TABLE IF NOT EXISTS uds_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "ts TEXT NOT NULL,"
            "ifname TEXT,"
            "req_hex TEXT,"
            "resp_hex TEXT,"
            "req_len INTEGER,"
            "resp_len INTEGER,"
            "status TEXT);",
            NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

void gw_log_uds(const char *ifname,
                const uint8_t *req, uint16_t req_len,
                const uint8_t *resp, uint16_t resp_len,
                const char *status)
{
    sqlite3_stmt *stmt;
    char ts[32];
    char *req_hex;
    char *resp_hex;

    if (!g_db || !req || req_len == 0U) {
        return;
    }
    /* 0x36 每块都记会淹没日志；OTA 只记 0x34/0x37 */
    if (req[0] == 0x36U) {
        return;
    }

    req_hex = hexdup(req, req_len);
    resp_hex = hexdup(resp ? resp : (const uint8_t *)"", resp_len);
    if (!req_hex || !resp_hex) {
        free(req_hex);
        free(resp_hex);
        return;
    }
    iso_time(ts, sizeof ts);

    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO uds_events"
            "(ts,ifname,req_hex,resp_hex,req_len,resp_len,status)"
            " VALUES(?,?,?,?,?,?,?);",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, ts, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, ifname ? ifname : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, req_hex, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, resp_hex, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, (int)req_len);
        sqlite3_bind_int(stmt, 6, (int)resp_len);
        sqlite3_bind_text(stmt, 7, status ? status : "", -1, SQLITE_TRANSIENT);
        (void)sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    free(req_hex);
    free(resp_hex);
}

void gw_log_close(void)
{
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

#else

void gw_log_init(const char *path) { (void)path; }
void gw_log_close(void) { }
void gw_log_uds(const char *ifname,
                const uint8_t *req, uint16_t req_len,
                const uint8_t *resp, uint16_t resp_len,
                const char *status)
{
    (void)ifname;
    (void)req;
    (void)req_len;
    (void)resp;
    (void)resp_len;
    (void)status;
}

#endif
