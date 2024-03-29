#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "logman_int.h"

const char* level_tag[LOGLEVEL_COUNT] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
};

log_static logman_src log_obj;

log_static void log_error_callback_default(void) {}

log_static void log_write_int_err(const char* message, ...)
{
    if (log_obj.err_message == NULL) {
        return;
    }

    va_list va;
    va_start(va, message);
    vsnprintf(log_obj.err_message, INTERR_BUF_SIZE, message, va);
    va_end(va);
    
    if (log_obj.error_callback != NULL) {
        log_obj.error_callback();
    }
}

char* log_get_internal_error(void)
{
    return log_obj.err_message;
}

log_static logman_error log_buffers_init(void)
{
    log_obj.err_message = (char*)calloc(INTERR_BUF_SIZE, sizeof(char));
    if (log_obj.err_message == NULL) {
        return LOGERR_LOGBUFFINIT;
    }

    log_obj.date_buf = (char*)calloc(DATE_BUF_SIZE, sizeof(char));
    if (log_obj.date_buf == NULL) {
        log_write_int_err("LOGMAN_ERROR::Unable to initialize the internal buffer: date_buf, %ldB\n",
                DATE_BUF_SIZE);
        return LOGERR_LOGBUFFINIT;
    }
 
    log_obj.message_buf = (char*)calloc(MESSAGE_BUF_SIZE, sizeof(char));
    if (log_obj.message_buf == NULL) {
        log_write_int_err("LOGMAN_ERROR::Unable to initialize the internal buffer: message_buf, %ldB\n",
                MESSAGE_BUF_SIZE);
        return LOGERR_LOGBUFFINIT;
    }
    
    return LOGERR_NOERR;
}

log_static void log_date_update(void)
{
    if (log_obj.date_buf == NULL) {
        log_write_int_err("LOGMAN_ERROR::Date buffer uninitialized\n");
        return;
    }
    
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    size_t len = snprintf(log_obj.date_buf, DATE_BUF_SIZE, "%.2i.%.2i.%i %.2i:%.2i:%.2i",
                             tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (len >= DATE_BUF_SIZE) {
        log_write_int_err("LOGMAN_ERROR::Date buffer overflow\n");
        return;
    }
}

log_static void log_write_std(char *buf) {
    size_t len = strlen(buf);
    if (fprintf(log_obj.out_stream, "%s", buf) != len) {
        log_write_int_err("LOGMAN_ERROR::Unable to write to the stream\n");
        return;
    }
}

log_static void log_write_file(char *buf) {
    // TODO: fflush?
    size_t len = strlen(buf);
    if (len != fwrite(buf, sizeof(char), len, log_obj.out_stream)) {
        log_write_int_err("LOGMAN_ERROR::Unable to write to log file\n");
        return;
    }
}

log_static logman_error log_set_out_file(const char* file_name)
{
    FILE* f = fopen(file_name, "w");
    if (f == NULL) {
        log_write_int_err("LOGMAN_ERROR::Unable to create/open log file\n");
        return LOGERR_LOGFILECREATE;
    }
    
    log_obj.out_stream = f;
    log_obj.writer = log_write_file;
    return LOGERR_NOERR;
}

log_static logman_error log_form_message_core(size_t start, const char* message, va_list va)
{
    size_t max_len = MESSAGE_BUF_SIZE - start;
    size_t mes_len = vsnprintf(&log_obj.message_buf[start], max_len, message, va);
    log_obj.message_buf[start + mes_len] = '\n';
    log_obj.message_buf[start + mes_len + 1] = '\0';
    if (mes_len >= max_len) {
        return LOGERR_LOGBUFOVERFLOW;
    }
    return LOGERR_NOERR;
}

log_static void log_form_debug_message(logman_level level, const char* file, const char* func, const int line, 
    const char* message, va_list va)
{
    log_date_update();
    size_t len = snprintf(log_obj.message_buf, MESSAGE_BUF_SIZE, "%s::%s::%s::%s::%i::",
                             log_obj.date_buf, level_tag[level], file, func, line);
    if (len >= MESSAGE_BUF_SIZE) {
        goto err;
    }                             

    if (log_form_message_core(len, message, va) == LOGERR_NOERR) {
        return;
    }

    err:
    log_write_int_err("LOGMAN_ERROR::Message buffer overflow\n");
}

log_static void log_form_product_message(logman_level level, const char* file, const char* func, const int line, 
    const char* message, va_list va)
{
    if (level <= LOGLEVEL_DEBUG) {
        return;
    }

    log_date_update();
    size_t len = snprintf(log_obj.message_buf, MESSAGE_BUF_SIZE, "%s::%s::", log_obj.date_buf, level_tag[level]);
    if (len >= MESSAGE_BUF_SIZE) {
        goto err;
    }   

    if (log_form_message_core(len, message, va) == LOGERR_NOERR) {
        return;
    }

    err:
    log_write_int_err("LOGMAN_ERROR::Message buffer overflow\n");
}

logman_error log_init_default(void)
{
    log_obj.out_type = LOGOUT_STREAM;
    log_obj.out_stream = stderr;
    log_obj.writer = log_write_std;
    log_obj.error_callback = log_error_callback_default;
    log_obj.message_former = log_form_debug_message;
    return log_buffers_init();
}

logman_error log_init(logman_settings* settings)
{
    if (settings == NULL) {
        logman_error err = log_init_default();
        if (err == LOGERR_NOERR) {
            log_write_int_err("LOGMAN_WARNING::Empty settings, setting default\n");
        }
        return err;
    }
    
    log_obj.error_callback = (settings->error_callback == NULL) ? log_error_callback_default : settings->error_callback;
    logman_error err = log_buffers_init();
    if (err != LOGERR_NOERR) {
        return err;
    }

    switch (settings->type) {
        case LOGTYPE_DEBUG:
            log_obj.message_former = log_form_debug_message;
            break;
        case LOGTYPE_PRODUCT:
            log_obj.message_former = log_form_product_message;
            break;
        default:
            log_write_int_err("LOGMAN_ERROR::Unknown logman type\n");
            return LOGERR_LOGUNKNOWNTYPE;
    }
    
    switch (settings->out_type) {
        case LOGOUT_STREAM:
            log_obj.out_stream = (settings->output.out_stream != stderr && settings->output.out_stream != stdout) ?
                stderr : settings->output.out_stream;
            log_obj.writer = log_write_std;
            
            break;
        case LOGOUT_FILE:
            err = log_set_out_file(settings->output.file_name);
            if (err != LOGERR_NOERR) {
                return err;
            }
            log_obj.out_type = LOGOUT_FILE;
            break;
        default:
            log_write_int_err("LOGMAN_ERROR::Unknown logman output type\n");
            return LOGERR_LOGUNKNOWNOUTTYPE;
    }
    
    return LOGERR_NOERR;
}

void log_destruct(void)
{
    if (log_obj.out_type == LOGOUT_FILE) {
        if (fclose(log_obj.out_stream) != 0) {
            log_write_int_err("LOGMAN_ERROR::Unable to close log file\n");
        }
    }
    
    free(log_obj.date_buf);
    free(log_obj.err_message);
    free(log_obj.message_buf);
    memset(&log_obj, 0, sizeof(log_obj));
}

void __log_log(logman_level level, const char* file, const char* func, const int line, const char* message, ...)
{
    if (log_obj.message_buf == NULL) {
        log_write_int_err("LOGMAN_ERROR::Message buffer uninitialized\n");
        return;
    }

    va_list va;
    va_start(va, message);
    log_obj.message_former(level, file, func, line, message, va);
    log_obj.writer(log_obj.message_buf);
    va_end(va);
}


