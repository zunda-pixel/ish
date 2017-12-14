#ifndef MEMORY_H
#define MEMORY_H

#include "misc.h"
#include <unistd.h>
#include <string.h>

// top 20 bits of an address, i.e. address >> 12
typedef dword_t page_t;
#define BAD_PAGE 0x10000

struct mem {
    unsigned refcount;
    struct pt_entry *pt; // TODO replace with red-black tree
    struct tlb_entry *tlb;
    page_t dirty_page;
};
#define MEM_PAGES (1 << 20) // at least on 32-bit

// Create a new address space
struct mem *mem_new(void);
// Increment the refcount
void mem_retain(struct mem *mem);
// Decrement the refcount, destroy everything in the space if 0
void mem_release(struct mem *mem);

#define PAGE_BITS 12
#undef PAGE_SIZE // defined in system headers somewhere
#define PAGE_SIZE (1 << PAGE_BITS)
#define PAGE(addr) ((addr) >> PAGE_BITS)
#define OFFSET(addr) ((addr) & (PAGE_SIZE - 1))
typedef dword_t pages_t;
#define PAGE_ROUND_UP(bytes) (((bytes - 1) / PAGE_SIZE) + 1)

#define BYTES_ROUND_DOWN(bytes) (PAGE(bytes) << PAGE_BITS)
#define BYTES_ROUND_UP(bytes) (PAGE_ROUND_UP(bytes) << PAGE_BITS)

struct data {
    void *data;
    unsigned refcount;
};
struct pt_entry {
    struct data *data;
    size_t offset;
    unsigned flags;
};
// page flags
// P_READ and P_EXEC are ignored for now
#define P_READ (1 << 0)
#define P_WRITE (1 << 1)
#undef P_EXEC // defined in sys/proc.h on darwin
#define P_EXEC (1 << 2)
#define P_GROWSDOWN (1 << 3)
#define P_COW (1 << 4)
#define P_WRITABLE(flags) (flags & P_WRITE && !(flags & P_COW))

page_t pt_find_hole(struct mem *mem, pages_t size);

#define PT_FORCE 1

// Map real memory into fake memory (unmaps existing mappings). The memory is
// freed with munmap, so it must be allocated with mmap
int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, unsigned flags);
// Map fake file into fake memory
int pt_map_file(struct mem *mem, page_t start, pages_t pages, int fd, off_t off, unsigned flags);
// Map empty space into fake memory
int pt_map_nothing(struct mem *mem, page_t page, pages_t pages, unsigned flags);
// Unmap fake memory, return -1 if any part of the range isn't mapped and 0 otherwise
int pt_unmap(struct mem *mem, page_t start, pages_t pages, int force);
// Set the flags on memory
int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags);
// Copy pages from src memory to dst memory using copy-on-write
int pt_copy_on_write(struct mem *src, page_t src_start, struct mem *dst, page_t dst_start, page_t pages);

struct tlb_entry {
    page_t page;
    page_t page_if_writable;
    uintptr_t data_minus_addr;
};
#define TLB_BITS 10
#define TLB_SIZE (1 << TLB_BITS)
#define TLB_INDEX(addr) ((addr >> PAGE_BITS) & (TLB_SIZE - 1))
#define TLB_READ 0
#define TLB_WRITE 1
#define TLB_PAGE(addr) (addr & 0xfffff000)
#define TLB_PAGE_EMPTY 1
void *tlb_handle_miss(struct mem *mem, addr_t addr, int type);

forceinline void *__mem_read_ptr(struct mem *mem, addr_t addr) {
    struct tlb_entry entry = mem->tlb[TLB_INDEX(addr)];
    if (entry.page == TLB_PAGE(addr)) {
        void *address = (void *) (entry.data_minus_addr + addr);
        postulate(address != NULL);
        return address;
    }
    return tlb_handle_miss(mem, addr, TLB_READ);
}
bool __mem_read_cross_page(struct mem *mem, addr_t addr, char *value, unsigned size);
forceinline bool __mem_read(struct mem *mem, addr_t addr, void *out, unsigned size) {
    if (OFFSET(addr) > PAGE_SIZE - size)
        return __mem_read_cross_page(mem, addr, out, size);
    void *ptr = __mem_read_ptr(mem, addr);
    if (ptr == NULL)
        return false;
    memcpy(out, ptr, size);
    return true;
}
#define mem_read(mem, addr, value) __mem_read(mem, addr, (value), sizeof(*(value)))

forceinline void *__mem_write_ptr(struct mem *mem, addr_t addr) {
    struct tlb_entry entry = mem->tlb[TLB_INDEX(addr)];
    if (entry.page_if_writable == TLB_PAGE(addr)) {
        mem->dirty_page = TLB_PAGE(addr);
        void *address = (void *) (entry.data_minus_addr + addr);
        postulate(address != NULL);
        return address;
    }
    return tlb_handle_miss(mem, addr, TLB_WRITE);
}
bool __mem_write_cross_page(struct mem *mem, addr_t addr, const char *value, unsigned size);
forceinline bool __mem_write(struct mem *mem, addr_t addr, const void *value, unsigned size) {
    if (OFFSET(addr) > PAGE_SIZE - size)
        return __mem_write_cross_page(mem, addr, value, size);
    void *ptr = __mem_write_ptr(mem, addr);
    if (ptr == NULL)
        return false;
    memcpy(ptr, value, size);
    return true;
}
#define mem_write(mem, addr, value) __mem_write(mem, addr, (value), sizeof(*(value)))

extern size_t real_page_size;

#endif
