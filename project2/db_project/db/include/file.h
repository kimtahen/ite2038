#ifndef DB_FILE_H_
#define DB_FILE_H_

#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>

#include "page.h"
using namespace std;

// keeping this data structure not to open same file multiple times
extern map<string, int64_t> opened_tables;

// Open existing database file or create one if it doesn't exist
int64_t file_open_table_file(const char* pathname);

// Allocate an on-disk page from the free page list
pagenum_t file_alloc_page(int64_t table_id);

// Free an on-disk page to the free page list
void file_free_page(int64_t table_id, pagenum_t pagenum);

// Read an on-disk page into the in-memory page structure(dest)
void file_read_page(int64_t table_id, pagenum_t pagenum, struct page_t* dest);

// Write an in-memory page(src) to the on-disk page
void file_write_page(int64_t table_id, pagenum_t pagenum, const struct page_t* src);

// Close the database file
void file_close_database_file();

// Checking functions
uint64_t free_page_count(int64_t table_id);

#endif  // DB_FILE_H_
