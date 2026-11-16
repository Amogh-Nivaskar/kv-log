#include <stdlib.h>
#include <string.h>
#include "hashmap.h"
#include <stdio.h>

struct entry
{
    char *key;
    void *value;
    entry_t *nxt;
};

static unsigned int hashmap_hash(const char *key)
{
    unsigned int hash = 0;

    while (*key)
    {
        hash = hash * 31 + *key;
        key++;
    }

    return hash;
}

void hashmap_init(hashmap_t *map, free_fn free_value)
{
    map->capacity = INITIAL_CAPACITY;
    map->size = 0;
    map->free_value = free_value;

    entry_t **entries = calloc(map->capacity, sizeof(entry_t *));

    if (entries == NULL)
    {
        perror("calloc");
    }

    map->entries = entries;
}

static void free_value_if_owned(hashmap_t *map, void *value)
{
    if (map->free_value != NULL)
        map->free_value(value);
}

static int hashmap_resize(hashmap_t *map, int newCap)
{
    entry_t **entries = calloc(newCap, sizeof(entry_t *));

    if (entries == NULL)
    {
        perror("calloc");
        return 0;
    }

    for (int i = 0; i < map->capacity; i++)
    {
        entry_t *e = map->entries[i];
        while (e != NULL)
        {
            int idx = hashmap_hash(e->key) % newCap;
            if (entries[idx] == NULL)
            {
                entries[idx] = e;
            }
            else
            {
                entry_t *x = entries[idx];
                while (x->nxt != NULL)
                {
                    x = x->nxt;
                }
                x->nxt = e;
            }
            entry_t *nxt = e->nxt;
            e->nxt = NULL;
            e = nxt;
        }
    }

    free(map->entries);

    map->capacity = newCap;
    map->entries = entries;
    return 1;
}

int hashmap_add(hashmap_t *map, char *key, void *value)
{
    float loadfactor = (float)map->size / map->capacity;
    if (loadfactor >= UPSIZE_THRESHOLD)
    {
        int rs = hashmap_resize(map, map->capacity * 2);
        if (rs == 0)
        {
            perror("resize");
            return 0;
        }
    }

    int idx = hashmap_hash(key) % map->capacity;

    entry_t *newEntry;

    if (map->entries[idx] == NULL)
    {
        newEntry = calloc(1, sizeof(entry_t));
        if (newEntry == NULL)
        {
            perror("calloc");
            return 0;
        }
        newEntry->key = strdup(key);
        newEntry->value = value;
        map->entries[idx] = newEntry;
        map->size += 1;
    }
    else
    {
        entry_t *entry = map->entries[idx];

        while (entry->nxt != NULL)
        {
            if (strcmp(entry->key, key) == 0)
            {
                free_value_if_owned(map, entry->value);
                entry->value = value;
                return 1;
            }
            entry = entry->nxt;
        }

        newEntry = calloc(1, sizeof(entry_t));
        if (newEntry == NULL)
        {
            perror("calloc");
            return 0;
        }

        newEntry->key = strdup(key);
        newEntry->value = value;
        entry->nxt = newEntry;
        map->size += 1;
    }
    return 1;
}

int hashmap_get(hashmap_t *map, char *key, void **value)
{
    int idx = hashmap_hash(key) % map->capacity;

    entry_t *entry = map->entries[idx];

    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            *value = entry->value;
            return 1;
        }
        entry = entry->nxt;
    }
    return 0;
}

int hashmap_delete(hashmap_t *map, char *key)
{

    float loadfactor = (float)map->size / map->capacity;
    if (loadfactor <= UPSIZE_THRESHOLD && map->capacity > INITIAL_CAPACITY)
    {
        int rs = hashmap_resize(map, map->capacity / 2);
        if (rs == 0)
        {
            perror("resize");
            return 0;
        }
    }

    int idx = hashmap_hash(key) % map->capacity;

    entry_t *entry = map->entries[idx];

    if (entry == NULL)
    {
        return 0;
    }

    if (strcmp(entry->key, key) == 0)
    {
        map->entries[idx] = entry->nxt;
        free(entry->key);
        free_value_if_owned(map, entry->value);
        free(entry);
        return 1;
    }

    entry_t *prev = entry;
    entry_t *curr = entry->nxt;
    while (curr != NULL)
    {
        if (strcmp(curr->key, key) == 0)
        {
            prev->nxt = curr->nxt;
            free(curr->key);
            free_value_if_owned(map, curr->value);
            free(curr);
            return 1;
        }
        prev = curr;
        curr = curr->nxt;
    }
    return 0;
}

void hashmap_destroy(hashmap_t *map)
{
    for (int i = 0; i < map->capacity; i++)
    {
        entry_t *entry = map->entries[i];
        while (entry != NULL)
        {
            entry_t *nxtEntry = entry->nxt;
            free(entry->key);
            free_value_if_owned(map, entry->value);
            free(entry);
            entry = nxtEntry;
        }
    }

    free(map->entries);
}
