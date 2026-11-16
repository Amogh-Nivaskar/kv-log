#ifndef KV_H
#define KV_H

int bootup();
int put(char *key, char *value);
int get(char *key, char **value);
void shutdown();
void reset();

#endif