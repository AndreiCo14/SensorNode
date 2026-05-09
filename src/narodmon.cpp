#include "narodmon.h"
#include "logger.h"

#include "system_state.h"

#ifdef ESP8266
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
#else
  #include <HTTPClient.h>
  #include <WiFiClient.h>
#endif

bool send_to_narodmon(const char* p) {
  //  const char *p = json_data;
    uint32_t chipId = STATE_GET(chipId);
    char NMmerged[256];
    snprintf(NMmerged, sizeof(NMmerged), "{\"devices\":[{\"mac\":\"AIRMQ%d\",\"sensors\":[", chipId);
    size_t pos = strlen(NMmerged); // Текущая позиция записи в NMmerged
    bool first = true;                 // Флаг первой пары (чтобы не ставить запятую в начале)

    while (*p) {
        while (*p == '{' || *p == ' ' || *p == '\t') p++; // Пропускаем возможные пробелы/табуляции/скобки
        if (*p == '\0') break;
        if (*p != '"') { p++; continue; } // Ожидаем открывающую кавычку ключа
        p++; // пропускаем "

        // Вычисляем длину ключа
        const char *key_start = p;
        while (*p && *p != '"') p++;
        size_t key_len = p - key_start;

        // Пропускаем закрывающую кавычку и двоеточие
        if (*p == '"') p++;
        if (*p == ':') p++;

        // Ищем конец значения (запятая, конец строки или })
        const char *val_start = p;
        while (*p && *p != ',' && *p != '}') p++;
        size_t val_len = p - val_start;

        // Копируем во временные буферы для гарантии null-termination
        #define BUF_LEN 16
        char key_buf[BUF_LEN] = {0};
        char val_buf[BUF_LEN] = {0};
        if (key_len >= BUF_LEN) key_len = BUF_LEN - 1;
        if (val_len >= BUF_LEN) val_len = BUF_LEN - 1;
        memcpy(key_buf, key_start, key_len);
        memcpy(val_buf, val_start, val_len);

        // Запись в NMmerged
        size_t remaining = sizeof(NMmerged) - pos;
        if (remaining > 1) {
            int written = 0;

            // Добавляем запятую между объектами (кроме первого)
            if (!first) {
                written = snprintf(NMmerged + pos, remaining, ",");
                if (written > 0) pos += written;
                remaining = sizeof(NMmerged) - pos;
            }

            written = snprintf(NMmerged + pos, remaining, "{\"id\":\"%s\",\"value\":%s}", key_buf, val_buf); // Формируем пару {"id":"KEY","value":"VAL"}

            // Проверяем, что запись прошла успешно и без обрезки
            if (written > 0 && (size_t)written < remaining) {
                pos += written;
            } else {
                logMessage("warn","Переполнение буфера NMmerged");
                break;
            }
            first = false;
        } else {
            logMessage("warn","Буфер NMmerged полностью заполнен");
            break;
        }

        // Переходим к следующей паре (пропускаем запятую, если есть)
        if (*p == ',') p++;
    }

    // Опционально: закрываем скобки, чтобы JSON был синтаксически валидным
     snprintf(NMmerged + pos, sizeof(NMmerged) - pos, "]}]}"); // {"devices":[{"mac":"DEVICE_MAC","sensors":[{{"id":"Temp","value":24.56}}}]}}
 logMessageFmt("NM","%s",NMmerged);

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(10000);

    if (!http.begin(client, "http://narodmon.ru/json")) {
        logMessage("NM", "Ошибка начала HTTP-соединения");
        return false;
    }

    http.addHeader(F("Content-Type"), F("application/x-www-form-urlencoded"));
    http.addHeader(F("Host"), F("narodmon.ru"));

    int httpCode = http.POST(NMmerged);
    bool success = false;

    if (httpCode > 0) {
        String response = http.getString();
        logMessageFmt("NM", "HTTP %d | Ответ: %s\n", httpCode, response.c_str());

        if (httpCode == 200 && response.indexOf(F("\"error\":\"OK\"")) >= 0 ) {
            logMessage("NM", "Данные успешно приняты Narodmon!");
            success = true;
        } else {
            logMessage("NM", "ERROR");
        }
    } else {
        logMessageFmt("NM", "Ошибка отправки: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
    }

    http.end();
    return success;
}