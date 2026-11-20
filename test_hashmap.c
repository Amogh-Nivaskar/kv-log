#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "hashmap.h"

void test_initialization_and_destroy()
{

    hashmap_t map;

    hashmap_init(&map, NULL);

    printf("test_init: passed\n");

    hashmap_destroy(&map);

    printf("test_destroy: passed\n");
}

void test_add_and_get()
{
    hashmap_t map;
    hashmap_init(&map, NULL);

    char *key = "Hello";
    char *value1 = "World";

    assert(hashmap_add(&map, key, value1) == 1);
    assert(map.size == 1);

    void *value2 = NULL;

    assert(hashmap_get(&map, key, &value2) == 1);
    assert(strcmp(value1, value2) == 0);

    hashmap_destroy(&map);

    printf("test_add_and_get: passed\n");
}

void test_get_absent()
{
    hashmap_t map;
    hashmap_init(&map, NULL);

    char *key1 = "Hello";
    char *value1 = "World";

    assert(hashmap_add(&map, key1, value1) == 1);

    char *key2 = "Hello2";
    void *value2 = NULL;

    assert(hashmap_get(&map, key2, &value2) == 0);

    hashmap_destroy(&map);

    printf("test_get_absent: passed\n");
}

void test_upsize()
{
    hashmap_t map;
    hashmap_init(&map, free);
    int initial_capacity = map.capacity;

    int iters = 100;

    for (int i = 0; i < iters; i++)
    {
        char key[32];
        char buf[32];

        snprintf(key, sizeof(key), "KEY_%d", i);
        snprintf(buf, sizeof(buf), "VAL_%d", i);

        char *value = strdup(buf);

        assert(hashmap_add(&map, key, value) == 1);
        assert(map.size == i + 1);
    }

    assert(map.capacity > initial_capacity);

    for (int i = 0; i < iters; i++)
    {
        char key[32];
        char expected[32];

        snprintf(key, sizeof(key), "KEY_%d", i);
        snprintf(expected, sizeof(expected), "VAL_%d", i);
        void *actual = NULL;

        assert(hashmap_get(&map, key, &actual) == 1);
        assert(strcmp(actual, expected) == 0);
    }

    hashmap_destroy(&map);

    printf("test_upsize: passed\n");
}

void test_delete()
{
    hashmap_t map;
    hashmap_init(&map, NULL);

    char *key = "Hello";
    char *value1 = "World";

    assert(hashmap_add(&map, key, value1) == 1);
    assert(map.size == 1);

    void *value2 = NULL;

    assert(hashmap_get(&map, key, &value2) == 1);
    assert(strcmp(value1, value2) == 0);

    assert(hashmap_delete(&map, key) == 1);

    assert(hashmap_get(&map, key, &value2) == 0);

    hashmap_destroy(&map);

    printf("test_delete: passed\n");
}

void test_downsize()
{
    hashmap_t map;
    hashmap_init(&map, free);
    int initial_capacity = map.capacity;

    int iters = 100;

    for (int i = 0; i < iters; i++)
    {
        char key[32];
        char buf[32];

        snprintf(key, sizeof(key), "KEY_%d", i);
        snprintf(buf, sizeof(buf), "VAL_%d", i);

        char *value = strdup(buf);

        assert(hashmap_add(&map, key, value) == 1);
        assert(map.size == i + 1);
    }

    int grown_capacity = map.capacity;

    assert(grown_capacity > initial_capacity);

    for (int i = 0; i < iters - 1; i++)
    {
        char key[32];

        snprintf(key, sizeof(key), "KEY_%d", i);

        assert(hashmap_delete(&map, key) == 1);
    }

    assert(map.capacity < grown_capacity);

    hashmap_destroy(&map);

    printf("test_downsize: passed\n");
}

int main(void)
{
    test_initialization_and_destroy();
    test_add_and_get();
    test_get_absent();
    test_upsize();
    test_delete();
    test_downsize();
}