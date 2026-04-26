#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>

#define WALLET_HASH_SIZE 16
#define WALLET_HASH_STRLEN (WALLET_HASH_SIZE + 1)
#define MAX_WALLETS_PER_BLOCK 100

struct Block {
    int wallet_count;
    char wallet_hashes[MAX_WALLETS_PER_BLOCK][WALLET_HASH_SIZE + 1];
};

typedef struct {
    char sender[WALLET_HASH_STRLEN];
    char receiver[WALLET_HASH_STRLEN];
    int leadingZeros;
    long long amount;
} Tx;

struct RecvMessageBuffer {
    long mtype;
    int security_value;
    int decryption_key;
};

struct SendMessageBuffer {
    long mtype;
    long sum;
};

typedef struct TxList {
    int *idx;
    int cnt;
    int cap;
} TxList;

typedef struct Entry {
    char key[WALLET_HASH_STRLEN];
    TxList list;
    struct Entry *next;
} Entry;

typedef struct {
    Entry **buckets;
    size_t nbuckets;
} HashMap;

static HashMap *hashmap_create(size_t nbuckets) {
    HashMap *hm = malloc(sizeof(HashMap));
    if (!hm) return NULL;
    hm->nbuckets = nbuckets;
    hm->buckets = calloc(nbuckets, sizeof(Entry *));
    if (!hm->buckets) { free(hm); return NULL; }
    return hm;
}

static inline unsigned long hash_wallet(const char *s) {
    unsigned long h = 5381;
    for (int i = 0; i < WALLET_HASH_SIZE; ++i) {
        h = ((h << 5) + h) + (unsigned char)s[i];
    }
    return h;
}

static Entry *hashmap_find_entry(HashMap *hm, const char *key) {
    unsigned long h = hash_wallet(key);
    size_t b = h % hm->nbuckets;
    for (Entry *e = hm->buckets[b]; e; e = e->next) {
        if (memcmp(e->key, key, WALLET_HASH_STRLEN) == 0) return e;
    }
    return NULL;
}

static Entry *hashmap_create_entry(HashMap *hm, const char *key) {
    unsigned long h = hash_wallet(key);
    size_t b = h % hm->nbuckets;
    Entry *e = malloc(sizeof(Entry));
    if (!e) return NULL;
    memcpy(e->key, key, WALLET_HASH_STRLEN);
    e->list.idx = NULL;
    e->list.cnt = 0;
    e->list.cap = 0;
    e->next = hm->buckets[b];
    hm->buckets[b] = e;
    return e;
}

static int entry_add_index(Entry *e, int txidx) {
    if (e->list.cnt == e->list.cap) {
        int newcap = e->list.cap ? e->list.cap * 2 : 4;
        int *tmp = realloc(e->list.idx, newcap * sizeof(int));
        if (!tmp) return -1;
        e->list.idx = tmp;
        e->list.cap = newcap;
    }
    e->list.idx[e->list.cnt++] = txidx;
    return 0;
}

static int hashmap_add_index(HashMap *hm, const char *key, int txidx) {
    Entry *e = hashmap_find_entry(hm, key);
    if (!e) e = hashmap_create_entry(hm, key);
    if (!e) return -1;
    return entry_add_index(e, txidx);
}

static void hashmap_free(HashMap *hm) {
    if (!hm) return;
    for (size_t i = 0; i < hm->nbuckets; ++i) {
        Entry *e = hm->buckets[i];
        while (e) {
            Entry *n = e->next;
            free(e->list.idx);
            free(e);
            e = n;
        }
    }
    free(hm->buckets);
    free(hm);
}

static inline void right_rotate_wallet(const char *src, char *dst, int k) {
    k %= WALLET_HASH_SIZE;
    if (k < 0) k += WALLET_HASH_SIZE;
    if (k == 0) {
        memcpy(dst, src, WALLET_HASH_STRLEN);
        return;
    }
    for (int i = 0; i < WALLET_HASH_SIZE; ++i) {
        int idx = i - k;
        if (idx < 0) idx += WALLET_HASH_SIZE;
        dst[i] = src[idx];
    }
    dst[WALLET_HASH_SIZE] = '\0';
}

static inline int count_leading_zeros(const char *s) {
    int cnt = 0;
    while (*s && *s == '0') { ++cnt; ++s; }
    return cnt;
}

static void copy_wallet_field(char dest[WALLET_HASH_STRLEN], const char *src) {
    memcpy(dest, src, WALLET_HASH_SIZE);
    dest[WALLET_HASH_SIZE] = '\0';
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    char input_fname[256];
    char tx_fname[256];
    int tnum = atoi(argv[1]);
    if (tnum <= 0) return 1;
    snprintf(input_fname, sizeof(input_fname), "input_%d.txt", tnum);
    snprintf(tx_fname, sizeof(tx_fname), "transactions_%d.txt", tnum);
    FILE *fin = fopen(input_fname, "r");
    if (!fin) return 1;
    long total_tx_ll = 0;
    int N_blocks = 0;
    int shm_key_int = 0;
    int msg_key_int = 0;
    if (fscanf(fin, "%ld\n%d\n%d\n%d\n", &total_tx_ll, &N_blocks, &shm_key_int, &msg_key_int) != 4) {
        fclose(fin);
        return 1;
    }
    fclose(fin);
    if (total_tx_ll < 0 || N_blocks <= 0) return 1;
    int T = (int) total_tx_ll;
    Tx *txs = malloc(sizeof(Tx) * (size_t)T);
    if (!txs) return 1;
    FILE *ftx = fopen(tx_fname, "r");
    if (!ftx) { free(txs); return 1; }
    char timestamp_buf[64];
    char hash_buf[128];
    char sender_buf[WALLET_HASH_STRLEN];
    char receiver_buf[WALLET_HASH_STRLEN];
    long long amount_ll;
    int parsed = 0;
    while (parsed < T) {
        int r = fscanf(ftx, "%63s %127s %16s %16s %lld",
                       timestamp_buf, hash_buf, sender_buf, receiver_buf, &amount_ll);
        if (r == EOF) break;
        if (r != 5) {
            int c;
            while ((c = fgetc(ftx)) != EOF && c != '\n');
            continue;
        }
        char s_fixed[WALLET_HASH_STRLEN] = {0}, r_fixed[WALLET_HASH_STRLEN] = {0};
        strncpy(s_fixed, sender_buf, WALLET_HASH_SIZE);
        s_fixed[WALLET_HASH_SIZE] = '\0';
        strncpy(r_fixed, receiver_buf, WALLET_HASH_SIZE);
        r_fixed[WALLET_HASH_SIZE] = '\0';
        copy_wallet_field(txs[parsed].sender, s_fixed);
        copy_wallet_field(txs[parsed].receiver, r_fixed);
        txs[parsed].amount = amount_ll;
        txs[parsed].leadingZeros = count_leading_zeros(hash_buf);
        ++parsed;
    }
    fclose(ftx);
    if (parsed != T) T = parsed;
    size_t nbuckets = 1 << 20;
    if ((size_t)T < (1<<16)) nbuckets = 1 << 16;
    HashMap *hm = hashmap_create(nbuckets);
    if (!hm) { free(txs); return 1; }
    for (int i = 0; i < T; ++i) {
        if (hashmap_add_index(hm, txs[i].sender, i) != 0) {
            hashmap_free(hm);
            free(txs);
            return 1;
        }
        if (memcmp(txs[i].sender, txs[i].receiver, WALLET_HASH_STRLEN) != 0) {
            if (hashmap_add_index(hm, txs[i].receiver, i) != 0) {
                hashmap_free(hm);
                free(txs);
                return 1;
            }
        }
    }
    key_t shm_key = (key_t)shm_key_int;
    size_t shmsz = sizeof(struct Block) * (size_t)N_blocks;
    int shmid = shmget(shm_key, shmsz, 0666);
    if (shmid < 0) {
        hashmap_free(hm);
        free(txs);
        return 1;
    }
    struct Block *shmPtr = (struct Block *) shmat(shmid, NULL, 0);
    if (shmPtr == (void *) -1) {
        hashmap_free(hm);
        free(txs);
        return 1;
    }
    key_t msg_key = (key_t)msg_key_int;
    int msqid = msgget(msg_key, 0666);
    if (msqid < 0) {
        shmdt(shmPtr);
        hashmap_free(hm);
        free(txs);
        return 1;
    }
    struct RecvMessageBuffer hmsg;
    struct SendMessageBuffer rmsg;
    ssize_t recv_len;
    for (int b = 0; b < N_blocks; ++b) {
        recv_len = msgrcv(msqid, &hmsg, (sizeof(hmsg) - sizeof(long)), 2, 0);
        if (recv_len < 0) break;
        int security_value = hmsg.security_value;
        int decryption_key = hmsg.decryption_key;
        char decrypted_wallets[MAX_WALLETS_PER_BLOCK][WALLET_HASH_STRLEN];
        int wallet_count = 0;
        if (shmPtr[b].wallet_count > 0 && shmPtr[b].wallet_count <= MAX_WALLETS_PER_BLOCK) {
            wallet_count = shmPtr[b].wallet_count;
            for (int i = 0; i < wallet_count; ++i) {
                char src[WALLET_HASH_STRLEN] = {0};
                memcpy(src, shmPtr[b].wallet_hashes[i], WALLET_HASH_SIZE);
                src[WALLET_HASH_SIZE] = '\0';
                right_rotate_wallet(src, decrypted_wallets[i], decryption_key);
            }
        } else {
            for (int i = 0; i < MAX_WALLETS_PER_BLOCK; ++i) {
                char src[WALLET_HASH_STRLEN];
                memcpy(src, shmPtr[b].wallet_hashes[i], WALLET_HASH_SIZE);
                src[WALLET_HASH_SIZE] = '\0';
                if (src[0] == '\0') continue;
                right_rotate_wallet(src, decrypted_wallets[wallet_count], decryption_key);
                wallet_count++;
            }
        }
        long long sum = 0;
        for (int wi = 0; wi < wallet_count; ++wi) {
            Entry *e = hashmap_find_entry(hm, decrypted_wallets[wi]);
            if (!e) continue;
            for (int k = 0; k < e->list.cnt; ++k) {
                int tidx = e->list.idx[k];
                if (tidx < 0 || tidx >= T) continue;
                if (txs[tidx].leadingZeros >= security_value) {
                    sum += txs[tidx].amount;
                }
            }
        }
        rmsg.mtype = 1;
        rmsg.sum = (long) sum;
        if (msgsnd(msqid, &rmsg, (sizeof(rmsg) - sizeof(long)), 0) < 0) {
            break;
        }
    }
    shmdt(shmPtr);
    hashmap_free(hm);
    free(txs);
    return 0;
}
