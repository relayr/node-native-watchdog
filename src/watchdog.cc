/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

#include <node.h>
#include <uv.h>
#include <time.h>
#include <stdlib.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#else
#include <unistd.h>
#endif

#define EXIT_CODE 87

using namespace v8;

Isolate *isolate;                       // captured v8 isolate
double timeout;                         // configured timeout (in millis)
uv_thread_t monitor_thread_id;          // id of the monitor thread
uv_rwlock_t _last_ping_time_lock;       // rw lock for the ping time
unsigned long long _last_ping_time = 0; // last ping time (when the JS event loop was alive)
long long delta_ping_time;

unsigned long long epoch_millis()
{
    time_t seconds;
    time(&seconds);
    return (unsigned long long)seconds * 1000;
}

void init_last_ping_time()
{
    uv_rwlock_init(&_last_ping_time_lock);
    _last_ping_time = epoch_millis();
}
void destroy_last_ping_time()
{
    uv_rwlock_destroy(&_last_ping_time_lock);
}
unsigned long long read_last_ping_time()
{
    unsigned long long r;
    uv_rwlock_rdlock(&_last_ping_time_lock);
    r = _last_ping_time;
    uv_rwlock_rdunlock(&_last_ping_time_lock);
    return r;
}
void write_last_ping_time(unsigned long long value)
{
    uv_rwlock_wrlock(&_last_ping_time_lock);
    _last_ping_time = value;
    uv_rwlock_wrunlock(&_last_ping_time_lock);
}

void replace_newlines(char *buffer, size_t size) {
    //int linescount = 0;
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\r') buffer[i] = (i == size - 1 ? 0: ' ');
    }
}

void monitor_stop(Isolate *isolate, void *data)
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    fprintf(stderr, "{\"name\":\"Error\",\"message\":\"Event loop unresponsive for %lld ms, will seppuku with code %d\"}\n", delta_ping_time, EXIT_CODE);
    Message::PrintCurrentStackTrace(isolate, stderr);
#else
    char* buffer = NULL;
    size_t bufferSize = 0;
    FILE* stackDescr = open_memstream(&buffer, &bufferSize);
    Message::PrintCurrentStackTrace(isolate, stackDescr);
    fclose(stackDescr);
    replace_newlines(buffer, bufferSize);
    fprintf(stderr, "{\"name\":\"Error\",\"message\":\"Event loop unresponsive for %lld ms, will seppuku with code %d\",\"stack\":\"%s\"}\n", delta_ping_time, EXIT_CODE, buffer);
    free(buffer);
#endif
    // Choosing a value different than any of the ones at
    // https://github.com/nodejs/node/blob/master/doc/api/process.md#exit-codes
    exit(EXIT_CODE);
}

void monitor(void *arg)
{
    unsigned long long last_watchdog_time = epoch_millis();
    while (true)
    {
        unsigned long long now = epoch_millis();
        long long delta_watchdog_time = now - last_watchdog_time;
        last_watchdog_time = now;

        if (delta_watchdog_time > 5000) {

            // The last sleep call took more than 5s => indication of machine sleeping
            // and now waking up from sleep...

            // Pretend we have received a ping and we'll terminate the process if
            // we don't get another ping soon...
            write_last_ping_time(epoch_millis());

        } else {

            unsigned long long last_ping_time = read_last_ping_time();
            delta_ping_time = now - last_ping_time;

            if (delta_ping_time > timeout)
            {
                // More time than allowed via `timeout` has passed since we've been pinged
                isolate->RequestInterrupt(monitor_stop, NULL);
            }

        }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
        Sleep(1000);
#else
        sleep(1);
#endif
    }
}

void _Start(const FunctionCallbackInfo<Value> &args)
{
    isolate = args.GetIsolate();
    timeout = Local<Number>::Cast(args[0])->Value();

    init_last_ping_time();
    uv_thread_create(&monitor_thread_id, monitor, NULL);
    args.GetReturnValue().SetUndefined();
}

void _Ping(const FunctionCallbackInfo<Value> &args)
{
    write_last_ping_time(epoch_millis());
    args.GetReturnValue().SetUndefined();
}

void _Exit(const FunctionCallbackInfo<Value> &args)
{
    exit(args[0]->Int32Value());
}

void init(Local<Object> exports)
{
    NODE_SET_METHOD(exports, "start", _Start);
    NODE_SET_METHOD(exports, "ping", _Ping);
    NODE_SET_METHOD(exports, "exit", _Exit);
}

NODE_MODULE(watchdog, init)
