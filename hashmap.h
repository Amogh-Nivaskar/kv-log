#ifndef HASHMAP_H
#define HASHMAP_H

#define INITIAL_CAPACITY 10
#define UPSIZE_THRESHOLD 0.75
#define DOWNSIZE_THRESHOLD 0.25

typedef struct entry entry_t;

typedef void (*free_fn)(void *);

typedef struct
{
    entry_t **entries;
    int capacity;
    int size;
    free_fn free_value;
} hashmap_t;

void hashmap_init(hashmap_t *map, free_fn free_value);
int hashmap_add(hashmap_t *map, char *key, void *value);
int hashmap_get(hashmap_t *map, char *key, void **value);
int hashmap_delete(hashmap_t *map, char *key);
void hashmap_destroy(hashmap_t *map);

#endif