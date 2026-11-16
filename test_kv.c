#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "kv.h"

void test_bootup()
{
    reset();

    assert(bootup() == 1);

    printf("test_bootup: passed\n");
}

void test_put_and_get()
{
    reset();

    char *key = "hello";
    char *value = "world";

    assert(bootup() == 1);

    assert(put(key, value) == 1);

    char *value2;

    assert(get(key, &value2) == 1);
    assert(strcmp(value, value2) == 0);

    printf("test_put_and_get: passed\n");
}

void test_put_shutdown_and_get()
{
    reset();

    char *key = "hello";
    char *value = "world";

    assert(bootup() == 1);

    assert(put(key, value) == 1);

    shutdown();

    assert(bootup() == 1);

    char *value2;

    assert(get(key, &value2) == 1);
    assert(strcmp(value, value2) == 0);

    printf("test_put_shutdown_and_get: passed\n");
}

void test_put_shutdown_and_get_absent()
{
    reset();

    char *key1 = "hello";
    char *value1 = "world";

    assert(bootup() == 1);

    assert(put(key1, value1) == 1);

    shutdown();

    assert(bootup() == 1);

    char *value2;
    char *key2 = "hello100";

    assert(get(key2, &value2) == 0);

    printf("test_put_shutdown_and_get_absent: passed\n");
}

void test_key_override()
{
    reset();

    char *key1 = "hello";
    char *value1 = "world";

    assert(bootup() == 1);

    assert(put(key1, value1) == 1);

    shutdown();

    assert(bootup() == 1);

    char *value2 = "darkness";

    assert(put(key1, value2) == 1);

    shutdown();

    assert(bootup() == 1);

    char *value3;

    assert(get(key1, &value3) == 1);
    assert(strcmp(value3, value2) == 0);

    printf("test_key_override: passed\n");
}

void test_delete()
{
    reset();

    char *key = "hello";
    char *value1 = "world";

    assert(bootup() == 1);

    assert(put(key, value1) == 1);

    assert(delete(key) == 1);

    char *value2;

    assert(get(key, &value2) == 0);

    shutdown();

    assert(bootup() == 1);

    assert(get(key, &value2) == 0);

    printf("test_delete: passed\n");
}

int main()
{
    test_bootup();
    test_put_and_get();
    test_put_shutdown_and_get();
    test_put_shutdown_and_get_absent();
    test_key_override();
    test_delete();
}