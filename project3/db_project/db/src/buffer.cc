#include "buffer.h"

LRU_Buffer Buffer;

buf_key_t tidpn_to_key(pair<int64_t, pagenum_t> tidpn){
    return tidpn.second*100 + (uint64_t)tidpn.first; 
}
pair<int64_t, pagenum_t> key_to_tidpn(buf_key_t key){
    return {key%100, key/100};
}


buf_block_t* alloc_buf_block_t(bool create_frame, int64_t tid, pagenum_t pn){
    buf_block_t* ctrl_block = (buf_block_t*)malloc(sizeof(buf_block_t));
    if(create_frame){
        ctrl_block->frame = (frame_t*)malloc(sizeof(frame_t)); 
        ctrl_block->is_dirty = 0;
        ctrl_block->is_pinned = 0;
    } 
    else {
        ctrl_block->frame = nullptr;
        //-1 means not using this value
        ctrl_block->is_dirty = -1;
        ctrl_block->is_pinned = -1;
    }
    ctrl_block->table_id = tid;
    ctrl_block->pagenum = pn;
    return ctrl_block;
}
void clear_buf_block_t(buf_block_t* buf_block){
    buf_block->table_id = -1;
    buf_block->pagenum = -1;
    buf_block->is_pinned = 0;
    buf_block->is_dirty = 0; 
}

LRU_Buffer::LRU_Buffer(){
    frame_in_use = 0;
    //buf_block of tid -2 is head and tail.
    head = alloc_buf_block_t(false, -2, 0);
    tail = alloc_buf_block_t(false, -2, 0);
    head->next = tail;
    tail->prev = head;
}

void LRU_Buffer::add_frame_front(buf_block_t* target){
    buf_block_t* temp = head->next;
    target->next = head->next;
    head->next = target;
    target->prev = temp->prev;
    temp->prev = target;
}
void LRU_Buffer::remove_frame(buf_block_t* target){
    target->prev->next = target->next;  
    target->next->prev = target->prev;
}
void LRU_Buffer::print_buf_block(){
    buf_block_t* cur = head->next;
    while(cur != tail){
        printf("ctrl_block | tid: %ld, pn: %lu, pin_count: %d, is_dirty: %d, frame_ptr: %p\n", cur->table_id, cur->pagenum, cur->is_pinned, cur->is_dirty, cur->frame);
        cur = cur->next;
    }
}
void buf_print(){
    Buffer.print_buf_block();
}

int init_buffer(int num_buf){
    Buffer = LRU_Buffer();
    //3 buffer frame is the minimum size of buffer. so pre-allocate 5 buffers.
    Buffer.frame_total = 5 + num_buf;
    //Buffer.print_buf_block();
    return 0;
}

int64_t buf_open_table_file(const char* pathname){
    //limitation on the number of tables is implemented in file manager.
    int64_t table_id = file_open_table_file(pathname);  
    return table_id;
}

int buf_read_page(int64_t table_id, pagenum_t pagenum, struct page_t* dest){
    //cache hit!
    if(Buffer.page_buf_block_map.find(tidpn_to_key({table_id, pagenum}))!=Buffer.page_buf_block_map.end()){
        //printf("Cache hit\n");
        memcpy(dest, Buffer.page_buf_block_map[tidpn_to_key({table_id,pagenum})]->frame, PAGE_SIZE);
        Buffer.page_buf_block_map[tidpn_to_key({table_id, pagenum})]->is_pinned += 1;
        buf_block_t* target = Buffer.page_buf_block_map[tidpn_to_key({table_id, pagenum})];
        Buffer.remove_frame(target);
        Buffer.add_frame_front(target);
        return 0;      
    }
    
    //Buffer is not full 
    if(Buffer.frame_total > Buffer.frame_in_use){
        //printf("Buf control block not full, so insert\n");

        // new buf_block initialize
        buf_block_t* new_buf_block = alloc_buf_block_t(true, table_id, pagenum); 
        new_buf_block->is_pinned += 1;
        file_read_page(table_id, pagenum, (page_t*)new_buf_block->frame);

        // return cached page
        memcpy(dest, new_buf_block->frame, PAGE_SIZE);

        // update Buffer 
        Buffer.add_frame_front(new_buf_block); 
        Buffer.frame_in_use++;
        Buffer.page_buf_block_map.insert({tidpn_to_key({table_id, pagenum}),new_buf_block});

        return 0;
    } 
    if(Buffer.frame_total < Buffer.frame_in_use){
        //printf("Must not be occured case occured!\n");
        return -1;
    } 

    /*
     * eviction phase
     */
    //printf("Buf Eviction\n");

    // Buffer is full, evict the victim page, traversing from tail.
    buf_block_t* cur = Buffer.tail->prev; 
    // header frame has table id of -2
    // select victim 
    while(cur->table_id!=-2){
        if(cur->is_pinned == 0){
            break; 
        }
        cur = cur->prev;
    }
    // cannot find victim
    if(cur->table_id==-2){
        //printf("ERROR: BUFFER is full and all frames are in use\n");
        return -1;
    }
    // evict the victim and load page from disk 
    if(cur->is_dirty==1){
        file_write_page(cur->table_id, cur->pagenum, (page_t*)cur->frame);
        cur->is_dirty=0;
    } 
    Buffer.remove_frame(cur);
    //printf("find: %d\n", cur->pagenum);

    cur->is_pinned += 1;
    file_read_page(table_id, pagenum, (page_t*)cur->frame);
    Buffer.page_buf_block_map.erase(tidpn_to_key({cur->table_id, cur->pagenum}));
    cur->table_id = table_id;
    cur->pagenum = pagenum;
    Buffer.page_buf_block_map.insert({tidpn_to_key({cur->table_id, cur->pagenum}), cur});

    // return cached page
    memcpy(dest, cur->frame, PAGE_SIZE);

    // update Buffer
    Buffer.add_frame_front(cur);
    
    //printf("Buf Eviction END!!\n");
    return 0;
}
int alloc_frame(int64_t table_id, pagenum_t pagenum){
    //cache hit!
    if(Buffer.page_buf_block_map.find(tidpn_to_key({table_id, pagenum}))!=Buffer.page_buf_block_map.end()){
        //printf("ERROR: should never be the case!\n");
        return -1;
    }
    
    //Buffer is not full 
    if(Buffer.frame_total > Buffer.frame_in_use){
        //printf("Buf control block not full, so insert\n");

        // new buf_block initialize
        buf_block_t* new_buf_block = alloc_buf_block_t(true, table_id, pagenum); 
        new_buf_block->is_pinned += 1;

        // update Buffer 
        Buffer.add_frame_front(new_buf_block); 
        Buffer.frame_in_use++;
        Buffer.page_buf_block_map.insert({tidpn_to_key({table_id, pagenum}),new_buf_block});

        return 0;
    } 
    if(Buffer.frame_total < Buffer.frame_in_use){
        //printf("Must not be occured case occured!\n");
        return -1;
    } 

    /*
     * eviction phase
     */
    //printf("Buf Eviction\n");

    // Buffer is full, evict the victim page, traversing from tail.
    buf_block_t* cur = Buffer.tail->prev; 
    // header frame has table id of -2
    // select victim 
    while(cur->table_id!=-2){
        if(cur->is_pinned == 0){
            break; 
        }
        cur = cur->prev;
    }
    // cannot find victim
    if(cur->table_id==-2){
        //printf("ERROR: BUFFER is full and all frames are in use\n");
        return -1;
    }
    // evict the victim and load page from disk 
    if(cur->is_dirty==1){
        file_write_page(cur->table_id, cur->pagenum, (page_t*)cur->frame);
        cur->is_dirty=0;
    } 
    Buffer.remove_frame(cur);

    cur->is_pinned += 1;
    Buffer.page_buf_block_map.erase(tidpn_to_key({cur->table_id, cur->pagenum}));
    cur->table_id = table_id;
    cur->pagenum = pagenum;
    Buffer.page_buf_block_map.insert({tidpn_to_key({cur->table_id, cur->pagenum}), cur});

    // update Buffer
    Buffer.add_frame_front(cur);
    
    return 0;
}

pagenum_t buf_alloc_page(int64_t table_id){
    int cost = 0;
    pagenum_t allocated_pagenum;

    if(Buffer.page_buf_block_map.find(tidpn_to_key({table_id, 0}))!=Buffer.page_buf_block_map.end()){
        h_page_t* target = (h_page_t*)(Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->frame);
        if(target->free_page_number==0){
            if(Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->is_dirty == 1){
                //? pin unpin think
                file_write_page(table_id, 0, (page_t*) Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->frame);
                Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->is_dirty = 0;
            }
            allocated_pagenum = file_alloc_page(table_id); 
            file_read_page(table_id, 0, (page_t*) Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->frame);
        }
        else {
            f_page_t free_page_buf;
            allocated_pagenum = target->free_page_number;
            pread(fd_mapper(table_id), &free_page_buf, PAGE_SIZE, allocated_pagenum * PAGE_SIZE);

            target->free_page_number = free_page_buf.next_free_page_number; 
            Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->is_dirty = 1;
        }
    } else {
        allocated_pagenum = file_alloc_page(table_id);
    }
    int result = alloc_frame(table_id, allocated_pagenum);
    if(result == -1) {
        //printf("alloc page failed\n");
        file_free_page(table_id, allocated_pagenum); 
        return -1;
    }
    return allocated_pagenum;
}

// think about pin count!! // this should be called only when the resource is allocated
// think about synchronization, this function should be pending if other is using page.
int buf_write_page(int64_t table_id, pagenum_t pagenum, const struct page_t* src){
    if(Buffer.page_buf_block_map.find(tidpn_to_key({table_id, pagenum}))!=Buffer.page_buf_block_map.end()){
        //printf("Cache hit should be occured");
        memcpy(Buffer.page_buf_block_map[tidpn_to_key({table_id,pagenum})]->frame, src, PAGE_SIZE);
        Buffer.page_buf_block_map[tidpn_to_key({table_id, pagenum})]->is_dirty = 1;
        return 0;      
    }
    else {
        //printf("This should not be the case!\n");
        return -1;
    }
}

// if the target page is on cache, delete it. if not, just call file_free_page
// this should be called when the resource is allocated
void buf_free_page(int64_t table_id, pagenum_t pagenum){
    if(Buffer.page_buf_block_map.find(tidpn_to_key({table_id, 0}))!=Buffer.page_buf_block_map.end()){
        f_page_t free_page_buf;
        h_page_t* target = (h_page_t*)(Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->frame);
        pagenum_t temp = target->free_page_number;
        target->free_page_number = pagenum;
        Buffer.page_buf_block_map[tidpn_to_key({table_id, 0})]->is_dirty = 1;

        free_page_buf.next_free_page_number = temp;  
        file_write_page(table_id, pagenum, (page_t*)&free_page_buf);
    }
    else {
        file_free_page(table_id, pagenum);
    }
    free(Buffer.page_buf_block_map[tidpn_to_key({table_id, pagenum})]->frame);
    Buffer.remove_frame(Buffer.page_buf_block_map[tidpn_to_key({table_id, pagenum})]);
    Buffer.frame_in_use -= 1;
    free(Buffer.page_buf_block_map[tidpn_to_key({table_id, pagenum})]);

    Buffer.page_buf_block_map.erase(tidpn_to_key({table_id, pagenum}));
}

int buf_unpin(int64_t table_id, pagenum_t pagenum){
    if(Buffer.page_buf_block_map.find(tidpn_to_key({table_id, pagenum}))!=Buffer.page_buf_block_map.end()){
        Buffer.page_buf_block_map[tidpn_to_key({table_id, pagenum})]->is_pinned -= 1;
        return 0;
    }
    //printf("unpin failed\n");
    return -1;
}

void fini_buffer(){
    buf_block_t* cur = Buffer.head->next;
    //int count = 0;
    while(cur != Buffer.tail){
        if(cur->is_dirty==1){
            file_write_page(cur->table_id, cur->pagenum, (page_t*)cur->frame);
            cur->is_dirty=0;
        }  
        //printf("freeing : ctrl_block | tid: %ld, pn: %lu, pin_count: %d, is_dirty: %d, frame_ptr: %p\n", cur->table_id, cur->pagenum, cur->is_pinned, cur->is_dirty, cur->frame);
        //count+=1;
        buf_block_t* nextptr = cur->next;
        free(cur->frame);
        free(cur);
        cur = nextptr;
    }
    //printf("buffer usage: %d\n",count);
    free(Buffer.head);
    free(Buffer.tail);
}
