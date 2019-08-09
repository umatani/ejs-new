/*
 * eJS Project
 * Kochi University of Technology
 * The University of Electro-communications
 *
 * The eJS Project is the successor of the SSJS Project at The University of
 * Electro-communications.
 */

#include "prefix.h"
#define EXTERN extern
#include "header.h"

#define HASH_SKIP (27)

/* #define REHASH_THRESHOLD (0.5) */
#define REHASH_THRESHOLD (0.9)

/*
 * allocates a hash table
 */
HashTable *malloc_hashtable(void) {
  HashTable *ht = (HashTable*)gc_malloc(NULL,
                                        sizeof(HashTable), HTAG_HASHTABLE);
  ht->body = NULL;
  return ht;
}

/*
 * initializes a hash table with the specified size
 */
int hash_create(HashTable *table, unsigned int size) {
  int i;

  table->body = __hashMalloc(size);
  if (table->body == NULL) {
    LOG_EXIT("hash body malloc failed\n");
  }
  for (i = 0; i < size; i++) {
    table->body[i] = NULL;
  }
  table->size = size;
  table->filled = 0;
  table->entry_count = 0;
  return 0;
}

/*
 * obtains the value and attribute associated with a given key
 */
int hash_get_with_attribute(HashTable *table, HashKey key, HashData *data,
                            Attribute *attr) {
  uint32_t hval;
  HashCell *cell;

  hval = string_hash(key) % table->size;
  for (cell = table->body[hval]; cell != NULL; cell = cell->next)
    if ((JSValue)(cell->entry.key) == key) {
      /* found */
      if (data != NULL) *data = cell->entry.data;
      if (attr != NULL) *attr = cell->entry.attr;
      /* printf("hash_get_with_attr: success, *data = %d\n", *data); */
      return HASH_GET_SUCCESS;
    }
  /* not found */
  /* printf("hash_get_with_attr: fail\n"); */
  return HASH_GET_FAILED;
}

/*
 * obtains the value associated with a given key
 */
int hash_get(HashTable *table, HashKey key, HashData *data) {
  int r;
  r = hash_get_with_attribute(table, key, data, NULL);
  /*
   * if (r == HASH_GET_SUCCESS) {
   *   printf("hash_get: success, "); simple_print(*data); putchar('\n');
   * } else
   *  printf("hash_get: fail\n");
   */
  return r;
}

/*
 * registers a value to a hash table under a given key with an attribute
 */
int hash_put_with_attribute(HashTable* table, HashKey key, HashData data,
                            Attribute attr) {
  HashCell* cell;
  uint32_t index;

  index = string_hash(key) % table->size;
  for (cell = table->body[index]; cell != NULL; cell = cell->next) {
    if (cell->entry.key == key) {
      /* found */
      if (!is_readonly(cell->entry.attr)) {
        cell->deleted = false;
        cell->entry.data = data;
        cell->entry.attr = attr;
        return HASH_PUT_SUCCESS;
      } else
        return HASH_PUT_FAILED;
    }
  }
  /* not found */
  cell = __hashCellMalloc();
  cell->next = table->body[index];
  table->body[index] = cell;
  cell->deleted = false;
  cell->entry.key = key;
  cell->entry.data = data;
  cell->entry.attr = attr;
  if (cell->next == NULL) {
    table->entry_count++;
    if (table->entry_count > REHASH_THRESHOLD * table->size)
      rehash(table);
  }
  return HASH_PUT_SUCCESS;
}

/*
 * deletes the hash data
 */
int hash_delete(HashTable *table, HashKey key) {
  HashCell *cell, *prev;
  uint32_t index;

  index = string_hash(key) % table->size;
  for (prev = NULL, cell = table->body[index]; cell != NULL;
       prev = cell, cell = cell->next) {
    if (cell->entry.key == key) {
      /* found */
      if (!is_dont_delete(cell->entry.attr)) return HASH_GET_FAILED;
      if (prev == NULL) {
        table->body[index] = cell->next;
      } else {
        prev->next = cell->next;
      }
      hashCellFree(cell);
      return HASH_GET_SUCCESS;
    }
  }
  return HASH_GET_FAILED;
}

/*
 * copies a hash table
 * This function is used only for copying a hash table in a hidden class.
 * This function returns the number of copied properties.
 */
int hash_copy(Context *ctx, HashTable *from, HashTable *to) {
  int i, fromsize, tosize;
  HashCell *cell, *new;
  uint32_t index;
  int n, ec;

  fromsize = from->size;
  tosize = to->size;
  n = 0;
  ec = 0;
  for (i = 0; i < fromsize; i++) {
    for (cell = from->body[i]; cell != NULL; cell = cell->next) {
      /* we do not copy the transition entry. */
      if (is_transition(cell->entry.attr)) continue;
      index = string_hash(cell->entry.key) % tosize;
      new = __hashCellMalloc();
      new->deleted = false;
      new->entry = cell->entry;
      if (to->body[index] == NULL) ec++;   /* increments entry count */
      new->next = to->body[index];
      to->body[index] = new;
      n++;
    }
  }
  to->entry_count = ec;
  to->filled = from->filled;
  return n;
}

HashCell** __hashMalloc(int size) {
  HashCell** ret = (HashCell**)gc_malloc(NULL, sizeof(HashCell*) * size,
                                         HTAG_HASH_BODY);
  memset(ret, 0, sizeof(HashCell*) * size);
  return ret;
}

HashCell* __hashCellMalloc() {
  HashCell* cell = (HashCell*)gc_malloc(NULL, sizeof(HashCell), HTAG_HASH_CELL);
  cell->next = NULL;
  return cell;
}

int rehash(HashTable *table) {
  int size = table->size;
  int newsize = size * 2;
  HashIterator iter;
  HashCell *p;
  HashCell** newhash = __hashMalloc(newsize);
  HashCell** oldhash = table->body;

  iter = createHashIterator(table);
  while (nextHashCell(table, &iter, &p) != FAIL) {
    uint32_t index = string_hash(p->entry.key) % newsize;
    p->next = newhash[index];
    newhash[index] = p;
  }
  table->body = newhash;
  table->size = newsize;
  hashBodyFree(oldhash);
  return 0;
}

int init_hash_iterator(HashTable *t, HashIterator *h) {
  int i, size;

  size = t->size;
  for (i = 0; i < size; i++) {
    if (t->body[i] != NULL) {
      h->p = t->body[i];
      h->index = i;
      return TRUE;
    }
  }
  h->p = NULL;
  return FALSE;
}

HashIterator createHashIterator(HashTable *table) {
  int i, size = table->size;
  HashIterator iter;

  iter.p = NULL;
  for (i = 0; i < size; i++) {
    if (table->body[i] != NULL) {
      iter.p = table->body[i];
      iter.index = i;
      break;
    }
  }
  return iter;
}

int nextHashCell(HashTable *table, HashIterator *iter, HashCell **p) {
  int i;

  if (iter->p == NULL) return FAIL;
  *p = iter->p;
  if (iter->p->next != NULL) {
    iter->p = iter->p->next;
    return SUCCESS;
  }
  for (i = iter->index + 1; i < table->size; i++) {
    if (table->body[i] != NULL) {
      iter->index = i;
      iter->p = table->body[i];
      return SUCCESS;
    }
  }
  iter->p = NULL;
  return SUCCESS;
}

void hashBodyFree(HashCell** body) {
#if !defined(USE_BOEHMGC) && !defined(USE_NATIVEGC)
  free(body);
#endif
}

void hashCellFree(HashCell* cell) {
#if !defined(USE_BOEHMGC) && !defined(USE_NATIVEGC)
  free(cell);
#endif
}

/*
 * prints a hash table (for debugging)
 */
void print_hash_table(HashTable *tab) {
  HashCell *p;
  unsigned int i, ec;

  printf("HashTable %p: size = %d, entry_count = %d\n",
         tab, tab->size, tab->entry_count);
  ec = 0;
  for (i = 0; i < tab->size; i++) {
    if ((p = tab->body[i]) == NULL) continue;
    ec++;
    do {
      printf(" (%d: (", i);
      printf("0x%"PRIx64" = ", p->entry.key); simple_print(p->entry.key);
      printf(", ");
      printf("0x%"PRIx64, p->entry.data);
      printf("))\n");
    } while ((p = p->next) != NULL);
    /* if (ec >= tab->entry_count) break; */
  }
  printf("end HashTable\n");

}

char* ststrdup(const char* str) {
  uint64_t len = strlen(str)+1;
  char *dst = (char*)malloc(sizeof(char) * len);

  strcpy(dst, str);
  return dst;
}

/* Local Variables:      */
/* mode: c               */
/* c-basic-offset: 2     */
/* indent-tabs-mode: nil */
/* End:                  */
