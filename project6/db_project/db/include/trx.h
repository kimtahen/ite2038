#ifndef TRX_H_
#define TRX_H_

#include <stdint.h>
#include <unordered_map>
#include <unordered_set>
#include <limits.h>
#include <pthread.h>
#include <bitset>
#include "file.h"
#include "buffer.h"
#include "dbpt.h"
#include "log.h"

typedef struct lock_t lock_t;

/*
 * TRX_TABLE_MODULE_BEGIN
 */
struct trx_map_entry_t {
    lock_t* head;
    lock_t* tail;
    queue<pair<char*, uint16_t>> undo_values; 
    uint64_t last_lsn;   
};

extern pthread_mutex_t trx_table_latch;    
class TRX_Table {
public:
    void acquire_tt_latch();
    void release_tt_latch();
    unordered_map<int, trx_map_entry_t> trx_map;    
    int g_trx_id = 1;
    
    // trx_id: success, 0: fail
    int create_entry();

    // 0: success, -1: fail
    int connect_lock_obj(int trx_id, lock_t* lock_obj); 

    // trx_id: success, 0: fail
    int release_trx_lock_obj(int trx_id);
    // trx_id: success, 0: fail
    int abort_trx_lock_obj(int trx_id);
    int push_undo_value(int trx_id, char* value, uint16_t old_val_size);
};
extern TRX_Table trx_table;


int trx_begin();
int trx_commit(int trx_id);

/*
 * TRX_TABLE_MODULE_END
 */

int init_trx();
/*
 * LOCK_TABLE_MODULE_BEGIN
 */
using namespace std;

#define SHARED 0
#define EXCLUSIVE 1

// This is the hash combine function originated from boost::hash_combine
// https://www.boost.org/doc/libs/1_55_0/doc/html/hash/reference.html#boost.hash_combine
// https://www.boost.org/doc/libs/1_55_0/doc/html/hash/combine.html](https://www.boost.org/doc/libs/1_55_0/doc/html/hash/combine.html
// https://stackoverflow.com/questions/7222143/unordered-map-hash-function-c](https://stackoverflow.com/questions/7222143/unordered-map-hash-function-c

template <class T>
inline void boost_hash_combine(std::size_t& seed, const T& v){
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct pair_for_hash {
    template <class T1, class T2>
        std::size_t operator () (const std::pair<T1, T2> &p) const {
            std::size_t seed = 0;
            boost_hash_combine(seed, p.first);
            boost_hash_combine(seed, p.second);
            return seed;
        }
};


struct hash_table_entry {
    int64_t table_id;
    int64_t page_id;
    lock_t* head;
    lock_t* tail;
    unordered_map<int64_t, int> key_map;
    unordered_map<int, int64_t> key_map_reverse;
};

struct lock_t {
    lock_t* prev;
    lock_t* next;
    hash_table_entry* sentinel;
    pthread_cond_t cond;
    int lock_mode;
    int64_t record_id;
    lock_t* next_lock;
    int trx_id;
    //unordered_set<int64_t> key_set;
    bitset<300> key_set;
};


extern pthread_mutex_t lock_table_latch;
extern unordered_map<pair<int64_t,pagenum_t>, hash_table_entry, pair_for_hash> hash_table;


/* APIs for lock table */
lock_t* lock_acquire(int64_t table_id, pagenum_t page_id, int64_t key, int trx_id, int lock_mode);
int lock_release(lock_t* lock_obj);

/*
 * LOCK_TABLE_MODULE_END
 */

/*
 * TRX_API_BEGIN
 */

// 0: success, non-zero: failed
int db_find(int64_t table_id, int64_t key, char* ret_val, uint16_t* val_size, int trx_id);

// 0: success: non-zero: failed
int db_update(int64_t table_id, int64_t key, char* value, uint16_t new_val_size, uint16_t* old_val_size, int trx_id);

/*
 * TRX_API_END
 */
#endif

