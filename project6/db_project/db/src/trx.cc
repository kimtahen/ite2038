#include "trx.h"

/*
 * TRX_TABLE_MODULE_BEGIN
 */

pthread_mutex_t trx_table_latch;
void TRX_Table::acquire_tt_latch(){
    int result = pthread_mutex_lock(&trx_table_latch); 
    if(result != 0) {
        perror("tt latch lock failed: ");
        exit(EXIT_FAILURE);
    }
}
void TRX_Table::release_tt_latch(){
    int result = pthread_mutex_unlock(&trx_table_latch); 
    if(result != 0) {
        perror("tt latch unlock failed: ");
        exit(EXIT_FAILURE);
    }
}

int TRX_Table::create_entry(){
    int result;
    result = pthread_mutex_lock(&trx_table_latch); 
    if(result != 0) return 0;
    
    while(trx_map.find(g_trx_id)!=trx_map.end()){
        if(g_trx_id == INT_MAX){
            g_trx_id = 1;
        }
        g_trx_id += 1;
    }
    trx_map_entry_t tmet = {nullptr, nullptr};
    tmet.last_lsn = 0;
    trx_map.insert({g_trx_id, tmet});

    int return_value = g_trx_id;
    g_trx_id += 1;
    if(g_trx_id == INT_MAX){
        g_trx_id = 1;
    }

    log_manager.write_lb_023(return_value, 0);

    result = pthread_mutex_unlock(&trx_table_latch);
    if(result != 0) return 0;
    
    return return_value;
}

int TRX_Table::connect_lock_obj(int trx_id, lock_t* lock_obj){

    int result;
    result = pthread_mutex_lock(&trx_table_latch); 
    if(result != 0) return 0;

    // no entry exists
    if(trx_map.find(trx_id)==trx_map.end()){
        pthread_mutex_unlock(&trx_table_latch);
        return -1;
    }

    // save undo_values
    
    if(trx_map[trx_id].head == nullptr){
        trx_map[trx_id].head = lock_obj;
        trx_map[trx_id].tail = lock_obj;
    }
    else if(trx_map[trx_id].tail == lock_obj){
        //do nothing
    }
    else {
        trx_map[trx_id].tail->next_lock = lock_obj;
        trx_map[trx_id].tail = lock_obj;
    }

    result = pthread_mutex_unlock(&trx_table_latch);
    if(result != 0) return 0;

    return 0;
}

TRX_Table trx_table;

int TRX_Table::release_trx_lock_obj(int trx_id){
   //printf("release sequence start\n");
    int result;
    result = pthread_mutex_lock(&trx_table_latch); 
    if(result != 0) return 0;

    //printf("release trx: %d\n",trx_id);

    // no entry exists
    if(trx_map.find(trx_id)==trx_map.end()){
        pthread_mutex_unlock(&trx_table_latch);
        return 0;
    }
    
    lock_t* cursor = trx_map[trx_id].head;
    queue<pair<char*, uint16_t>> restored_queue = trx_map[trx_id].undo_values;

    trx_table.trx_map.erase(trx_id);

    result = pthread_mutex_unlock(&trx_table_latch);
    if(result != 0) return 0;

    result = pthread_mutex_lock(&lock_table_latch); 
    if(result!=0) return 0;

    while(cursor != nullptr){
        lock_t* next = cursor -> next_lock;
        lock_release(cursor);
        cursor = next;
    }

    result = pthread_mutex_unlock(&lock_table_latch); 
    if(result!=0) return 0;

    while(!restored_queue.empty()){
        auto i = restored_queue.front();
        restored_queue.pop();
        //delete i.first;
    }
    
    return trx_id; 
}
int TRX_Table::abort_trx_lock_obj(int trx_id){
    int result;
    trx_table.acquire_tt_latch();

    // no entry exists
    if(trx_map.find(trx_id)==trx_map.end()){
        pthread_mutex_unlock(&trx_table_latch);
        return 0;
    }
    
    lock_t* cursor = trx_map[trx_id].head;
    queue<pair<char*, uint16_t>> restored_queue = trx_map[trx_id].undo_values;

    trx_table.release_tt_latch();


    /*
    while(!restored_queue.empty()){
        auto restored_item =  restored_queue.front();
        restored_queue.pop();

        h_page_t header_node;
        buf_read_page(cursor->sentinel->table_id, 0, (page_t*) &header_node);      

        Node acquired_leaf = find_leaf(cursor->sentinel->table_id, header_node.root_page_number, cursor->record_id);
        uint16_t dummy;
        acquired_leaf.leaf_update(cursor->record_id, restored_item.first, restored_item.second, &dummy);
        acquired_leaf.write_to_disk(); 

        buf_unpin(cursor->sentinel->table_id, acquired_leaf.pn);
        buf_unpin(cursor->sentinel->table_id, 0);

        //delete restored_item.first;

    }
    */
    log_manager.ABORT(trx_id);

    /*
    trx_table.acquire_tt_latch();

    log_manager.write_lb_023(trx_id, 3);

    trx_table.trx_map.erase(trx_id);

    trx_table.release_tt_latch();
    */

    result = pthread_mutex_lock(&lock_table_latch); 
    if(result!=0) return 0;

    while(cursor != nullptr){
        //printf("record_id: %d\n",cursor->record_id);
        //printf("release\n");
        lock_t* next = cursor -> next_lock;
        lock_release(cursor);
        //printf("after release\n");
        cursor = next;
    }

    result = pthread_mutex_unlock(&lock_table_latch); 
    if(result!=0) return 0;
    
    return trx_id; 
}
int TRX_Table::push_undo_value(int trx_id, char *value, uint16_t old_val_size){
    int result;
    result = pthread_mutex_lock(&trx_table_latch); 
    if(result != 0) return 0;

    if(trx_map.find(trx_id)==trx_map.end()){
        pthread_mutex_unlock(&trx_table_latch);
        return 0;
    }

    trx_map[trx_id].undo_values.push({value, old_val_size});

    result = pthread_mutex_unlock(&trx_table_latch);
    if(result != 0) return 0;

    return trx_id;
}

int trx_begin(){
    return trx_table.create_entry(); 
}
int trx_commit(int trx_id){

    trx_table.acquire_tt_latch();
    {
        log_manager.write_lb_023(trx_id, 2);
    }
    trx_table.release_tt_latch();

    log_manager.flush_lb();
    return trx_table.release_trx_lock_obj(trx_id); 
}
int trx_abort(int trx_id){
    return trx_table.abort_trx_lock_obj(trx_id);
}



/*
 * TRX_TABLE_MODULE_END
 */

int init_trx() {
    int g_result = 0;

    pthread_mutexattr_t mtx_attribute;
    g_result = pthread_mutexattr_init(&mtx_attribute);
    g_result = pthread_mutexattr_settype(&mtx_attribute, PTHREAD_MUTEX_NORMAL);
    g_result = pthread_mutex_init(&lock_table_latch, &mtx_attribute);
    g_result = pthread_mutex_init(&trx_table_latch, &mtx_attribute);

    if (g_result!=0) return -1;
    return 0;
}
/*
 * LOCK_TABLE_MODULE_BEGIN
 */

int debug_count = 0;

pthread_mutex_t lock_table_latch;
unordered_map<pair<int64_t,pagenum_t>, hash_table_entry, pair_for_hash> hash_table;


bool lock_acquire_deadlock_detection(lock_t* dependency, int trx_id){
    //printf("lock_acquire_deadlock_detection %d\n", trx_id);
    int debug_int = 0;
    //printf("deadlock detection dependency trx_id: %d, dependency record_id %d, compared trx_id: %d\n",dependency->trx_id, dependency->record_id, trx_id);
    while(dependency->next_lock != nullptr){
        //printf("dependency looping: %d\n",debug_int++);
        dependency = dependency -> next_lock;
    }

    if(dependency->trx_id == trx_id){
        //printf("detected\n");
        return true;
    }

    lock_t* cursor = dependency->prev;
    while(cursor != nullptr){
        //printf("cursor looping,finding dependency: %d\n",debug_int++);
        //printf("debug_count lock_acquire %d\n", debug_count++);
        if(cursor->record_id != dependency->record_id){
            cursor = cursor -> prev;
            continue;
        }
        if(dependency->lock_mode==EXCLUSIVE){
            break;
        }
        else if(dependency->lock_mode==SHARED){
            if(cursor->lock_mode == EXCLUSIVE){
                break;
            }     
        }
        cursor = cursor -> prev;
    }
    // now cursor points to the lock that occurs dependency. if it is nullptr there is no dependency.
    if(cursor!=nullptr){
        if(dependency->lock_mode == EXCLUSIVE){
            if(cursor->lock_mode==EXCLUSIVE){
                //deadlock check
                return lock_acquire_deadlock_detection(cursor, trx_id);
            }
            else if(cursor->lock_mode==SHARED){
                //printf("deadlock check\n");
                //deadlock check
                while(cursor!=nullptr){
        //printf("cursor looping: %d\n",debug_int++);
                    if(cursor->record_id != dependency->record_id){
                        cursor = cursor -> prev;
                        continue;
                    }
                    if(cursor->lock_mode != SHARED){
                        break;
                    }
                    if(lock_acquire_deadlock_detection(cursor, trx_id)){
                        return true;
                    }
                    cursor = cursor->prev;             
                }
                //printf("no deadlock");
                return false;
            }
        }
        else if(dependency->lock_mode == SHARED){
            //printf("deadlock check\n");
            //deadlock check
            return lock_acquire_deadlock_detection(cursor, trx_id);
        }

    }
    else {
        //printf("no deadlock");
    }
    //else case/ because of warning changed this way
    return false;
}
void lock_detach(hash_table_entry* target, lock_t* lock_obj){
    if(lock_obj->prev == nullptr && lock_obj->next == nullptr){
        target->head = nullptr;
        target->tail = nullptr;
    }
    else if(lock_obj->prev == nullptr){
        //case lock_obj is leftmost
        target->head = lock_obj->next;
        lock_obj->next->prev = nullptr;
        lock_obj->next = nullptr;
    }
    else if(lock_obj->next == nullptr){
        //case lock_obj is rightmost
        target->tail = lock_obj->prev;
        lock_obj->prev->next = nullptr;
        lock_obj->prev = nullptr;
    }
    else {
        lock_obj->prev->next = lock_obj->next;
        lock_obj->next->prev = lock_obj->prev;
        lock_obj->next = nullptr;
        lock_obj->prev = nullptr;
    }

}

//deadlock or inner error: -1, success: 0
int lock_acquire(int64_t table_id, pagenum_t page_id, int64_t key, int trx_id, int lock_mode, bool* add_undo_value) {
    //printf("lock_acquire trx_id: %d\n",trx_id);
    int result = 0;

    result = pthread_mutex_lock(&lock_table_latch); 
    if(result!=0) return -1;
    //printf("lock_acquire trx: %d, table_id: %d, page_id: %d, record_id: %d\n", trx_id, table_id, page_id, key);

    if(hash_table.find({table_id, page_id})==hash_table.end()){
        hash_table_entry hte;
        hte.table_id = table_id;
        hte.page_id = page_id;
        hte.head = nullptr;
        hte.tail = nullptr;
        hash_table.insert({{table_id, page_id}, hte}); 
    }
    hash_table_entry* target = &hash_table[{table_id, page_id}]; 

    lock_t* lck = new lock_t;
    lck->next = nullptr;
    lck->prev = nullptr;
    lck->sentinel = &hash_table[{table_id, page_id}];
    lck->lock_mode = lock_mode;
    lck->record_id = key;
    lck->next_lock = nullptr;
    lck->trx_id = trx_id;
    lck->key_set.set(lck->sentinel->key_map[key], true);
    //lck->cond = PTHREAD_COND_INITIALIZER;
    result = pthread_cond_init(&(lck->cond), NULL);
    if(result!=0) return -1;

    //connect lock object at the end of list
    bool is_immediate;
    is_immediate = (target->head == nullptr);
    if(is_immediate){
        target->head = lck;
        target->tail = lck;
    }
    else {
        target->tail->next = lck;
        lck->prev = target->tail;
        target->tail = lck; 
    }


    //first check if trx already acquired lock. one or zero same trx lock_obj
    lock_t* same_trx_lock_obj = lck->prev;
    while(same_trx_lock_obj != nullptr){
        //printf("debug_count lock_acquire %d\n", debug_count++);
        if(same_trx_lock_obj->record_id != key){
            same_trx_lock_obj = same_trx_lock_obj -> prev;
            continue;
        }
        if(same_trx_lock_obj->trx_id == lck->trx_id){
            break; 
        }
        same_trx_lock_obj = same_trx_lock_obj -> prev;
    }
    if(same_trx_lock_obj != nullptr){
        if(same_trx_lock_obj->lock_mode == lck->lock_mode){
            lock_detach(target, lck);
            //delete lck;

            result = pthread_mutex_unlock(&lock_table_latch); 
            if(result!=0) return -1;
            
            *add_undo_value = false;

            return 0;
        }
        else {

            if(lck->lock_mode == EXCLUSIVE){


                /*
                lock_detach(target, same_trx_lock_obj); 
                same_trx_lock_obj->lock_mode = EXCLUSIVE;
                */
                lck->lock_mode = EXCLUSIVE;
                lck->record_id = key;
                lck->next_lock = same_trx_lock_obj->next_lock;
                
                trx_table.acquire_tt_latch();

                lock_t* cursor = trx_table.trx_map[trx_id].head;
                bool head_connected_flag = false;
                bool tail_connected_flag = false;
                if(trx_table.trx_map[trx_id].head == same_trx_lock_obj) head_connected_flag = true;
                if(trx_table.trx_map[trx_id].tail == same_trx_lock_obj) tail_connected_flag = true;

                if(head_connected_flag){
                    if(tail_connected_flag){
                        trx_table.trx_map[trx_id].head = lck;
                        trx_table.trx_map[trx_id].tail = lck;
                    }                    
                    else {
                        trx_table.trx_map[trx_id].head = lck;

                    }
                }
                else {
                    if(tail_connected_flag){
                        while(cursor != nullptr){
                            if(cursor->next_lock == same_trx_lock_obj){
                                cursor->next_lock = lck;
                                break;
                            }
                            cursor = cursor -> next_lock;
                        }
                        trx_table.trx_map[trx_id].tail = lck;
                    }
                    else {
                        while(cursor != nullptr){
                            if(cursor->next_lock == same_trx_lock_obj){
                                cursor->next_lock = lck;
                                break;
                            }
                            cursor = cursor -> next_lock;
                        }
                    }
                }
                

                trx_table.release_tt_latch();


                lock_release(same_trx_lock_obj);


                //delete lck;

                *add_undo_value = true;
                //do not return for further processing
            }
            else {
                lock_detach(target, lck);
                //lck->prev->next = nullptr; target->tail = lck->prev; 
                //delete lck; 

                result = pthread_mutex_unlock(&lock_table_latch); 
                if(result!=0) return -1;

                *add_undo_value = false;

                return 0;
            }
        }
    }
    if(!is_immediate){
        lock_t* cursor = lck->prev;

        lock_t* shared_same_trx_lock_obj = lck->prev;

        while(cursor != nullptr){
            //printf("debug_count lock_acquire %d\n", debug_count++);
            if(cursor->record_id != key){
                /*
                if(cursor->lock_mode == SHARED && cursor->trx_id == trx_id){
                    shared_same_trx_lock_obj = cursor;
                }
                */
                cursor = cursor -> prev;
                continue;
            }
            
            if(lck->lock_mode==EXCLUSIVE){
                break;
            }
            else if(lck->lock_mode==SHARED){
                if(cursor->lock_mode == EXCLUSIVE){
                    break;
                }     
                else if(cursor->prev == nullptr){
                    break;
                }
            }
            cursor = cursor -> prev;
        }

        if(cursor!=nullptr){
            if(lck->lock_mode == EXCLUSIVE){
                if(cursor->lock_mode==EXCLUSIVE){
                    //printf("current: exclusive, traget: exclusive\n");
                    //deadlock check
                    bool is_deadlock = lock_acquire_deadlock_detection(cursor, trx_id);
                    if(is_deadlock){
                        //printf("deadlock!\n");

                        /*
                        lck->prev->next = nullptr;
                        target->tail = lck->prev;
                        */
                        lock_detach(target, lck);

                        //delete lck; 

                        result = pthread_mutex_unlock(&lock_table_latch); 
                        if(result!=0) return -1;

                        return -1;
                    }
                }
                else if(cursor->lock_mode==SHARED){
                    while(cursor!=nullptr){
                        if(cursor->record_id != key){
                            cursor = cursor -> prev;
                            continue;
                        }
                        if(cursor->lock_mode != SHARED){
                            break;
                        } 
                        bool is_deadlock = lock_acquire_deadlock_detection(cursor, trx_id);
                        //printf("current: exclusive, traget: shared, %d",is_deadlock);
                        if(is_deadlock){
                            //printf("deadlock!\n");
                            /*
                            lck->prev->next = nullptr;
                            target->tail = lck->prev;
                            */
                            lock_detach(target, lck);

                            //delete lck; 

                            result = pthread_mutex_unlock(&lock_table_latch); 
                            if(result!=0) return -1;

                            return -1;
                        }
                        cursor = cursor->prev;             
                    }
                }
                //trx_table connection
                trx_table.connect_lock_obj(trx_id, lck);

                result = pthread_cond_wait(&lck->cond, &lock_table_latch);
                if(result!=0) return -1;
            }
            else if(lck->lock_mode == SHARED){
                bool is_deadlock = lock_acquire_deadlock_detection(cursor, trx_id);
                //printf("current: shared, traget: exclusive, %d",is_deadlock);
                if(is_deadlock){
                    //printf("deadlock!\n");
                    /*
                    lck->prev->next = nullptr;
                    target->tail = lck->prev;
                    */
                    lock_detach(target, lck);

                    //delete lck; 

                    result = pthread_mutex_unlock(&lock_table_latch); 
                    if(result!=0) return -1;
                    
                    return -1;
                }

                //trx_table connection
                trx_table.connect_lock_obj(trx_id, lck);

                result = pthread_cond_wait(&lck->cond, &lock_table_latch);
                if(result!=0) return -1;
            }
            
        }
        else {
            //no dependecy
            //trx_table connection
            trx_table.connect_lock_obj(trx_id, lck);
        }
    }
    else {
        //trx_table connection
        trx_table.connect_lock_obj(trx_id, lck);
    }



    result = pthread_mutex_unlock(&lock_table_latch); 
    if(result!=0) return -1;

    return 0;
};

// wrapper function release_trx_lock_obj acquires lock table latch
int lock_release(lock_t* lock_obj) {
    if(lock_obj==nullptr){
        return 0;
    }

    int result = 0;

    //printf("lock_release start\n");
    //printf("    lock_release %d, %d, %d\n",lock_obj->sentinel->table_id, lock_obj->sentinel->page_id, lock_obj->record_id);

    bool is_immediate;
    hash_table_entry* target = lock_obj->sentinel;

    is_immediate = ((lock_obj->next == nullptr) && (lock_obj->prev == nullptr));

    if(is_immediate){
        //this is the case that only one lock object exists
        target->head = nullptr;
        target->tail = nullptr;
    }
    else {
        //this is the case that lock objects are more than 2
        if(lock_obj->lock_mode == SHARED){
            int slock_count = 0;
            bool is_xlock_on_right_side = false;
            lock_t* xlock_cursor = lock_obj->next;

            while(xlock_cursor != nullptr){
                //printf("debug_count on shared%d\n", debug_count++);
                if(xlock_cursor->record_id != lock_obj->record_id) {
                    xlock_cursor = xlock_cursor->next;
                    continue;
                }
                if(xlock_cursor->lock_mode == EXCLUSIVE){
                    is_xlock_on_right_side = true;
                    break;
                }
                slock_count += 1;
                xlock_cursor = xlock_cursor->next;
            } 

            if(is_xlock_on_right_side == false){
                //printf("no rightside xlock case\n");
                lock_detach(target, lock_obj);
            }
            else if(is_xlock_on_right_side == true){
                //printf("rightside xlock case\n");
                lock_t* cursor = lock_obj->prev;
                while(cursor != nullptr){
                    //printf("debug_count on shared xlock rightside %d\n", debug_count++);
                    if(cursor->record_id != lock_obj->record_id){
                        cursor = cursor->prev;
                        continue;
                    }
                    //this is not needed because release is occured in lock acquired status
                    if(cursor->lock_mode == EXCLUSIVE){
                        break;
                    }
                    slock_count += 1;
                    cursor = cursor->prev;
                } 

                lock_detach(target, lock_obj);

                if(slock_count > 0){
                    //just release
                }
                else {
                    //signal xlock
                    //printf("signal xlock!\n");
                    result = pthread_cond_signal(&xlock_cursor->cond);
                    if(result!=0) return -1;
                }
            }
        }
        else if(lock_obj->lock_mode == EXCLUSIVE){
            lock_t* cursor = lock_obj->next;

            bool is_slock_acquired = false;

            while(cursor!=nullptr){
                /*
                printf("before 777\n");
                printf("check cursor nullptr %d\n",cursor == nullptr);
                printf("777: cursor rid: %d, lockobj rid: %d\n",cursor->record_id, lock_obj->record_id);
                */

                if(cursor->record_id != lock_obj->record_id){
                    cursor = cursor->next;
                    continue;
                }
                if(cursor->lock_mode == EXCLUSIVE){
                    if(!is_slock_acquired){
                        result = pthread_cond_signal(&cursor->cond);
                        if(result!=0) return -1;
                    }
                    break;
                } 
                else if(cursor->lock_mode == SHARED){
                    is_slock_acquired = true;
                }
                result = pthread_cond_signal(&cursor->cond);
                if(result!=0) return -1;

                cursor = cursor -> next;
            }
            
            lock_detach(target, lock_obj); 
        }
    }

    //delete lock_obj;


    return 0;
}

/*
 * LOCK_TABLE_MODULE_END
 */


/*
 * TRX_API_BEGIN
 */

// 0: success, non-zero: failed
int db_find(int64_t table_id, int64_t key, char* ret_val, uint16_t* val_size, int trx_id){
    h_page_t header_node;
    buf_read_page(table_id, 0, (page_t*) &header_node);      
    buf_unpin(table_id, 0);

    if(header_node.root_page_number==0){
        return 1;
    }
    Node leaf = find_leaf(table_id, header_node.root_page_number, key);
    int result = leaf.leaf_find_slot(key);

    if(result == -1){
        buf_unpin(table_id, leaf.pn);
        return 1;
    }
    buf_unpin(table_id, leaf.pn);

    bool dummy;
    int res = lock_acquire(table_id, leaf.pn, key, trx_id, SHARED, &dummy);        
    if(res==-1){
        //printf("abort!");
        trx_table.abort_trx_lock_obj(trx_id);
        return -1;
    }

    buf_read_page(table_id, 0, (page_t*) &header_node);      
    buf_unpin(table_id, 0);

    Node acquired_leaf = find_leaf(table_id, header_node.root_page_number, key);
    acquired_leaf.leaf_find(key, ret_val, val_size);
    buf_unpin(table_id, acquired_leaf.pn);

    return 0;
}

// 0: success: non-zero: failed
int db_update(int64_t table_id, int64_t key, char* value, uint16_t new_val_size, uint16_t* old_val_size, int trx_id){
    //printf("db_update %d\n",trx_id);
    h_page_t header_node;
    buf_read_page(table_id, 0, (page_t*) &header_node);      
    buf_unpin(table_id, 0);
    if(header_node.root_page_number==0){
        return 1;
    }
    //printf("update db_update dbg\n");
    Node leaf = find_leaf(table_id, header_node.root_page_number, key);
    int result = leaf.leaf_find_slot(key);

    if(result == -1){
        buf_unpin(table_id, leaf.pn);
        return 1;
    }
    buf_unpin(table_id, leaf.pn);

    bool add_undo_value = true;
    //printf("before lock_acquire, trx_id: %d\n",trx_id);
    int res = lock_acquire(table_id, leaf.pn, key, trx_id, EXCLUSIVE, &add_undo_value);        
    //printf("after lock_acquire, trx_id: %d\n",trx_id);
    if(res==-1){
        //printf("abort! %d\n",trx_id);
        trx_table.abort_trx_lock_obj(trx_id);
        return -1;
    }

    buf_read_page(table_id, 0, (page_t*) &header_node);      
    buf_unpin(table_id, 0);

    Node acquired_leaf = find_leaf(table_id, header_node.root_page_number, key);
    //if(add_undo_value){
    char* ret_val  = new char[130];
    slot_t ret_slot;
    result = leaf.leaf_find_ret(key, ret_val, old_val_size, &ret_slot);
    trx_table.push_undo_value(trx_id, ret_val, *old_val_size);
    //}
    uint64_t page_lsn = log_manager.write_lb_14(trx_id, 1, table_id, acquired_leaf.pn, ret_slot.get_offset(), *old_val_size, (uint8_t*)ret_val, (uint8_t*)value, 0);
    
    acquired_leaf.leaf_update(key, value, new_val_size, old_val_size);
    acquired_leaf.default_page.page_lsn = page_lsn; 
    acquired_leaf.write_to_disk(); 

    buf_unpin(table_id, acquired_leaf.pn);

    return 0;
}

/*
 * TRX_API_END
 */
