#include "asm_profiler.h"

#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void ap_symbol_table_init(ap_symbol_table *table) {
    memset(table, 0, sizeof(*table));
}

void ap_symbol_table_destroy(ap_symbol_table *table) {
    free(table->path);
    table->path = NULL;
    table->base_vaddr = 0;
    ap_hot_symbol_vec_destroy(&table->symbols);
}

static int ap_read_main_exe_path(pid_t pid, char *buffer, size_t size) {
    char link_path[64];
    ssize_t len;

    snprintf(link_path, sizeof(link_path), "/proc/%ld/exe", (long) pid);
    len = readlink(link_path, buffer, size - 1);
    if (len < 0 || (size_t) len >= size) {
        return -1;
    }

    buffer[len] = '\0';
    return 0;
}

static int ap_read_base_vaddr(pid_t pid, const char *exe_path, uint64_t *base_out) {
    char maps_path[64];
    FILE *stream = NULL;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len = 0;
    uint64_t best = UINT64_MAX;

    snprintf(maps_path, sizeof(maps_path), "/proc/%ld/maps", (long) pid);
    stream = fopen(maps_path, "r");
    if (!stream) {
        return -1;
    }

    while ((len = getline(&line, &cap, stream)) >= 0) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        unsigned long long offset = 0;
        char perms[5] = {0};
        char path[PATH_MAX] = {0};
        int fields = sscanf(line, "%llx-%llx %4s %llx %*s %*s %4095[^\n]",
                            &start, &end, perms, &offset, path);

        (void) end;

        if (fields < 4) {
            continue;
        }

        if (fields < 5 || strcmp(path, exe_path) != 0) {
            continue;
        }

        if (perms[0] != 'r' || perms[2] != 'x') {
            continue;
        }

        uint64_t candidate = (uint64_t) start - (uint64_t) offset;
        if (candidate < best) {
            best = candidate;
        }
    }

    free(line);
    fclose(stream);

    if (best == UINT64_MAX) {
        errno = ENOENT;
        return -1;
    }

    *base_out = best;
    return 0;
}

static int ap_symbol_cmp_start(const void *lhs, const void *rhs) {
    const ap_hot_symbol *a = lhs;
    const ap_hot_symbol *b = rhs;
    if (a->start < b->start) {
        return -1;
    }
    if (a->start > b->start) {
        return 1;
    }
    return 0;
}

static int ap_hot_cmp_samples(const void *lhs, const void *rhs) {
    const ap_hot_symbol *a = lhs;
    const ap_hot_symbol *b = rhs;
    if (a->samples < b->samples) {
        return 1;
    }
    if (a->samples > b->samples) {
        return -1;
    }
    if (a->start < b->start) {
        return -1;
    }
    if (a->start > b->start) {
        return 1;
    }
    return 0;
}

int ap_symbol_table_load_main_exe(ap_symbol_table *table, pid_t pid) {
    char path[PATH_MAX];
    int fd = -1;
    Elf *elf = NULL;
    GElf_Ehdr ehdr;

    ap_symbol_table_init(table);

    if (ap_read_main_exe_path(pid, path, sizeof(path)) != 0) {
        return -1;
    }

    if (elf_version(EV_CURRENT) == EV_NONE) {
        return -1;
    }

    table->path = ap_strdup(path);
    if (!table->path) {
        return -1;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ap_symbol_table_destroy(table);
        return -1;
    }

    elf = elf_begin(fd, ELF_C_READ, NULL);
    if (!elf) {
        close(fd);
        ap_symbol_table_destroy(table);
        return -1;
    }

    if (!gelf_getehdr(elf, &ehdr)) {
        elf_end(elf);
        close(fd);
        ap_symbol_table_destroy(table);
        return -1;
    }

    if (ehdr.e_type == ET_DYN) {
        if (ap_read_base_vaddr(pid, path, &table->base_vaddr) != 0) {
            elf_end(elf);
            close(fd);
            ap_symbol_table_destroy(table);
            return -1;
        }
    }

    Elf_Scn *scn = NULL;
    while ((scn = elf_nextscn(elf, scn)) != NULL) {
        GElf_Shdr shdr;
        if (!gelf_getshdr(scn, &shdr)) {
            continue;
        }

        if (shdr.sh_type != SHT_SYMTAB && shdr.sh_type != SHT_DYNSYM) {
            continue;
        }

        Elf_Data *data = elf_getdata(scn, NULL);
        if (!data || shdr.sh_entsize == 0) {
            continue;
        }

        size_t count = (size_t) (shdr.sh_size / shdr.sh_entsize);
        for (size_t i = 0; i < count; ++i) {
            GElf_Sym sym;
            const char *name;
            ap_hot_symbol item;

            if (!gelf_getsym(data, (int) i, &sym)) {
                continue;
            }

            if (GELF_ST_TYPE(sym.st_info) != STT_FUNC || sym.st_size == 0 || sym.st_value == 0) {
                continue;
            }

            name = elf_strptr(elf, shdr.sh_link, sym.st_name);
            if (!name || *name == '\0') {
                continue;
            }

            memset(&item, 0, sizeof(item));
            item.start = table->base_vaddr + sym.st_value;
            item.end = table->base_vaddr + sym.st_value + sym.st_size;
            strncpy(item.name, name, sizeof(item.name) - 1);

            if (ap_hot_symbol_vec_push(&table->symbols, &item) != 0) {
                elf_end(elf);
                close(fd);
                ap_symbol_table_destroy(table);
                return -1;
            }
        }
    }

    qsort(table->symbols.items, table->symbols.len, sizeof(*table->symbols.items), ap_symbol_cmp_start);
    elf_end(elf);
    close(fd);
    return 0;
}

static ssize_t ap_find_symbol(const ap_symbol_table *table, uint64_t ip) {
    size_t lo = 0;
    size_t hi = table->symbols.len;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const ap_hot_symbol *sym = &table->symbols.items[mid];

        if (ip < sym->start) {
            hi = mid;
        } else if (ip >= sym->end) {
            lo = mid + 1;
        } else {
            return (ssize_t) mid;
        }
    }

    return -1;
}

int ap_symbol_table_aggregate(const ap_symbol_table *table, const ap_ip_vec *ips, ap_hot_symbol_vec *out) {
    ap_hot_symbol_vec_destroy(out);

    if (table->symbols.len == 0) {
        return 0;
    }

    out->items = ap_callocarray(table->symbols.len, sizeof(*out->items));
    if (!out->items) {
        return -1;
    }

    out->cap = table->symbols.len;

    for (size_t i = 0; i < table->symbols.len; ++i) {
        out->items[i] = table->symbols.items[i];
    }

    out->len = table->symbols.len;

    for (size_t i = 0; i < ips->len; ++i) {
        ssize_t index = ap_find_symbol(table, ips->items[i].ip);
        if (index >= 0) {
            out->items[index].samples += ips->items[i].samples;
        }
    }

    qsort(out->items, out->len, sizeof(*out->items), ap_hot_cmp_samples);
    return 0;
}
