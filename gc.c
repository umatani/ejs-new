/*
 * eJS Project
 * Kochi University of Technology
 * The University of Electro-communications
 *
 * The eJS Project is the successor of the SSJS Project at The University of
 * Electro-communications.
 */

#include <stdlib.h>
#include <stdio.h>
#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "log.h"

#ifndef NDEBUG
#define GC_DEBUG 1
#define STATIC
#else
#undef GC_DEBUG
#define STATIC static
#endif

#if 0
#define GCLOG(...) LOG(__VA_ARGS__)
#define GCLOG_TRIGGER(...) LOG(__VA_ARGS__)
#define GCLOG_ALLOC(...) LOG(__VA_ARGS__)
#define GCLOG_SWEEP(...) LOG(__VA_ARGS__)
#else /* 0 */
#define GCLOG(...)
#define GCLOG_TRIGGER(...)
#define GCLOG_ALLOC(...)
#define GCLOG_SWEEP(...)
#endif /* 0 */

/*
 * defined in header.h
 */
/* typedef uint64_t JSValue; */
#define LOG_BYTES_IN_JSVALUE   3
/* #define BYTES_IN_JSVALUE       (1 << LOG_BYTES_IN_JSVALUE) */

/*
 * naming convention
 *   name for size: add a surfix representing the unit
 *                    bytes: in bytes
 *                    jsvalues: in the numberof JSValue's
 */

#ifndef JS_SPACE_BYTES
#define JS_SPACE_BYTES     (10 * 1024 * 1024)
#endif
#ifdef EXCESSIVE_GC
#define JS_SPACE_GC_THREASHOLD     (JS_SPACE_BYTES >> 4)
#else  /* EXCESSIVE_GC */
#define JS_SPACE_GC_THREASHOLD     (JS_SPACE_BYTES >> 1)
#endif /* EXCESSIVE_GC */

/*
 * If the remaining room is smaller than a certain size,
 * we do not use the remainder for efficiency.  Rather,
 * we add it below the chunk being allocated.  In this case,
 * the size in the header includes the extra words.
 */
#define MINIMUM_FREE_CHUNK_JSVALUES 4

#include "cell-header.h"

/*
 *  Macro
 */

#define GC_MARK_BIT (1 << HEADER0_GC_OFFSET)

/*
 *  Types
 */

#define HTAG_FREE          (0xff)

struct free_chunk {
  header_t header;
  struct free_chunk *next;
};

struct space {
  uintptr_t addr;
  size_t bytes;
  size_t free_bytes;
  struct free_chunk* freelist;
  char *name;
};

/*
 * variables
 */
STATIC struct space js_space;
#ifdef GC_DEBUG
STATIC struct space debug_js_shadow;
#endif /* GC_DEBUG */

/* old gc root stack (to be obsolete) */
#define MAX_TMP_ROOTS 1000
STATIC JSValue *tmp_roots[MAX_TMP_ROOTS];
STATIC int tmp_roots_sp;

/* new gc root stack */
#define MAX_ROOTS 1000
STATIC JSValue *gc_root_stack[MAX_ROOTS];
STATIC int gc_root_stack_ptr = 0;

STATIC int gc_disabled = 1;

int generation = 0;
int gc_sec;
int gc_usec;
#ifdef GC_PROF
uint64_t total_alloc_bytes;
uint64_t total_alloc_count;
uint64_t pertype_alloc_bytes[256];
uint64_t pertype_alloc_count[256];
#endif /* GC_PROF */

#ifdef GC_DEBUG
STATIC void **top;
STATIC void sanity_check();
#endif /* GC_DEBUG */

/*
 * prototype
 */
/* space */
STATIC void create_space(struct space *space, size_t bytes, char* name);
STATIC int in_js_space(void *addr_);
#ifdef GC_DEBUG
STATIC header_t *get_shadow(void *ptr);
#endif /* GC_DEBUG */
/* GC */
STATIC int check_gc_request(Context *);
STATIC void garbage_collect(Context *ctx);
STATIC void trace_HashCell_array(HashCell ***ptrp, uint32_t length);
STATIC void trace_HashCell(HashCell **ptrp);
STATIC void trace_HiddenClass(HiddenClass **ptrp);
#define trace_JSValue_array(ptrp, length)        \
  trace_JSValue_array_range((ptrp), 0, (length))
STATIC void trace_JSValue_array_range(JSValue **ptrp,
                                      size_t start, size_t length);
STATIC void trace_slot(JSValue* ptr);
STATIC void scan_roots(Context *ctx);
STATIC void scan_stack(JSValue* stack, int sp, int fp);
STATIC void weak_clear_StrTable(StrTable *table);
STATIC void weak_clear(void);
STATIC void sweep(void);
#ifdef GC_DEBUG
STATIC void check_invariant(void);
STATIC void print_memory_status(void);
STATIC void print_heap_stat(void);
#endif /* GC_DEBUG */


/*
 *  Space
 */

STATIC void create_space(struct space *space, size_t bytes, char *name)
{
  struct free_chunk *p;
  p = (struct free_chunk *) malloc(bytes);
  p->header = HEADER0_COMPOSE(bytes >> LOG_BYTES_IN_JSVALUE, 0, HTAG_FREE);
#ifdef GC_DEBUG
  HEADER0_SET_MAGIC(p->header, HEADER0_MAGIC);
#endif /* GC_DEBUG */
  p->next = NULL;
  space->addr = (uintptr_t) p;
  space->bytes = bytes;
  space->free_bytes = bytes;
  space->freelist = p;
  space->name = name;
}

STATIC int in_js_space(void *addr_)
{
  uintptr_t addr = (uintptr_t) addr_;
  return (js_space.addr <= addr && addr < js_space.addr + js_space.bytes);
}

#ifdef GC_DEBUG
STATIC header_t *get_shadow(void *ptr)
{
  if (in_js_space(ptr)) {
    uintptr_t a = (uintptr_t) ptr;
    uintptr_t off = a - js_space.addr;
    return (header_t *) (debug_js_shadow.addr + off);
  } else
    return NULL;
}
#endif /* GC_DEBUG */

/*
 * Returns a pointer to the first address of the memory area
 * available to the VM.  The header precedes the area.
 * The header has the size of the chunk including the header,
 * the area available to the VM, and extra bytes if any.
 * Other header bits are zero
 */
STATIC void* space_alloc(struct space *space,
                         size_t request_bytes, cell_type_t type)
{
  size_t  alloc_jsvalues;
  struct free_chunk **p;
  
  alloc_jsvalues =
    (request_bytes + BYTES_IN_JSVALUE - 1) >> LOG_BYTES_IN_JSVALUE;
  alloc_jsvalues += HEADER_JSVALUES;

  /* allocate from freelist */
  for (p = &space->freelist; *p != NULL; p = &(*p)->next) {
    struct free_chunk *chunk = *p;
    size_t chunk_jsvalues = HEADER0_GET_SIZE(chunk->header);
    if (chunk_jsvalues >= alloc_jsvalues) {
      if (chunk_jsvalues >= alloc_jsvalues + MINIMUM_FREE_CHUNK_JSVALUES) {
        /* This chunk is large enough to leave a part unused.  Split it */
        size_t new_chunk_jsvalues = chunk_jsvalues - alloc_jsvalues;
        uintptr_t addr =
          ((uintptr_t) chunk) + (new_chunk_jsvalues << LOG_BYTES_IN_JSVALUE);
        HEADER0_SET_SIZE(chunk->header, new_chunk_jsvalues);
        *(header_t *) addr = HEADER0_COMPOSE(alloc_jsvalues, 0, type);
#ifdef GC_DEBUG
        HEADER0_SET_MAGIC(*(header_t *) addr, HEADER0_MAGIC);
        HEADER0_SET_GEN_MASK(*(header_t *) addr, generation);
#endif /* GC_DEBUG */
        space->free_bytes -= alloc_jsvalues << LOG_BYTES_IN_JSVALUE;
        return (void *) (addr + HEADER_BYTES);
      } else {
        /* This chunk is too small to split. */
        *p = (*p)->next;
        chunk->header =
          HEADER0_COMPOSE(chunk_jsvalues,
                          chunk_jsvalues - alloc_jsvalues, type);
#ifdef GC_DEBUG
        HEADER0_SET_MAGIC(chunk->header, HEADER0_MAGIC);
        HEADER0_SET_GEN_MASK(chunk->header, generation);
#endif /* GC_DEBUG */
        space->free_bytes -= chunk_jsvalues << LOG_BYTES_IN_JSVALUE;
        return (void *) (((uintptr_t) chunk) + HEADER_BYTES);
      }
    }
  }

#ifdef DEBUG
  {
    struct free_chunk *chunk;
    for (chunk = space->freelist; chunk != NULL; chunk = chunk->next) {
      size_t chunk_jsvalues = HEADER0_GET_SIZE(chunk->header);
      LOG(" %lu", chunk_jsvalues * BYTES_IN_JSVALUE);
    }
  }
  LOG("\n");
  LOG("js_space.bytes = %lu\n", js_space.bytes);
  LOG("js_space.free_bytes = %lu\n", js_space.free_bytes);
  LOG("gc_disabled = %d\n", gc_disabled);
  LOG("request = %lu\n", request_bytes);
  LOG("type = 0x%x\n", type);
  LOG("memory exhausted\n");
#endif /* DEBUG */
  abort();
  return NULL;
}


/*
 * GC
 */

void init_memory()
{
  create_space(&js_space, JS_SPACE_BYTES, "js_space");
#ifdef GC_DEBUG
  create_space(&debug_js_shadow, JS_SPACE_BYTES, "debug_js_shadow");
#endif /* GC_DEBUG */
  tmp_roots_sp = -1;
  gc_root_stack_ptr = 0;
  gc_disabled = 0;
  generation = 1;
  gc_sec = 0;
  gc_usec = 0;
}

void gc_push_tmp_root(JSValue *loc)
{
  tmp_roots[++tmp_roots_sp] = loc;
}

void gc_push_tmp_root2(JSValue *loc1, JSValue *loc2)
{
  tmp_roots[++tmp_roots_sp] = loc1;
  tmp_roots[++tmp_roots_sp] = loc2;
}

void gc_push_tmp_root3(JSValue *loc1, JSValue *loc2, JSValue *loc3)
{
  tmp_roots[++tmp_roots_sp] = loc1;
  tmp_roots[++tmp_roots_sp] = loc2;
  tmp_roots[++tmp_roots_sp] = loc3;
}

void gc_pop_tmp_root(int n)
{
  tmp_roots_sp -= n;
}

void gc_push_checked(void *addr)
{
  gc_root_stack[gc_root_stack_ptr++] = (JSValue *) addr;
}

void gc_pop_checked(void *addr)
{
#ifdef GC_DEBUG
  if (gc_root_stack[gc_root_stack_ptr - 1] != (JSValue *) addr) {
    fprintf(stderr, "GC_POP pointer does not match\n");
    abort();
  }
#endif /* GC_DEBUG */
  gc_root_stack[--gc_root_stack_ptr] = NULL;
}

cell_type_t gc_obj_header_type(void *p)
{
  header_t *hdrp = ((header_t *) p) - 1;
  return HEADER0_GET_TYPE(*hdrp);
}

STATIC int check_gc_request(Context *ctx)
{
  if (ctx == NULL) {
    if (js_space.free_bytes < JS_SPACE_GC_THREASHOLD)
      GCLOG_TRIGGER("Needed gc for js_space -- cancelled: ctx == NULL\n");
    return 0;
  }
  if (gc_disabled) {
    if (js_space.free_bytes < JS_SPACE_GC_THREASHOLD)
      GCLOG_TRIGGER("Needed gc for js_space -- cancelled: GC disabled\n");
    return 0;
  }
  if (js_space.free_bytes < JS_SPACE_GC_THREASHOLD)
    return 1;
  GCLOG_TRIGGER("no GC needed (%d bytes free)\n", js_space.free_bytes);
  return 0;
}

void* gc_malloc(Context *ctx, uintptr_t request_bytes, uint32_t type)
{
  return gc_jsalloc(ctx, request_bytes, type);
}

JSValue* gc_jsalloc(Context *ctx, uintptr_t request_bytes, uint32_t type)
{
  JSValue *addr;
#ifdef GC_DEBUG
  top = (void**) &ctx;
#endif /* GC_DEBUG */

  if (check_gc_request(ctx))
    garbage_collect(ctx);
  addr = space_alloc(&js_space, request_bytes, type);
  GCLOG_ALLOC("gc_jsalloc: req %x bytes type %d => %p\n",
              request_bytes, type, addr);
#ifdef GC_DEBUG
  {
  header_t *hdrp = (header_t *) (addr - HEADER_JSVALUES);
  header_t *shadow = get_shadow(hdrp);
  *shadow = *hdrp;
  }
#endif /* GC_DEBUG */
#ifdef GC_PROF
  {
    size_t alloc_bytes =
      (request_bytes + BYTES_IN_JSVALUE * (HEADER_JSVALUES - 1)) &
      ~((1 << LOG_BYTES_IN_JSVALUE) - 1);
    total_alloc_bytes += alloc_bytes;
    total_alloc_count++;
    pertype_alloc_bytes[type] += alloc_bytes;
    pertype_alloc_count[type]++;
  }
#endif /* GC_PROF */
  return addr;
}

void disable_gc(void)
{
  gc_disabled++;
}

void enable_gc(Context *ctx)
{
#ifdef GC_DEBUG
  top = (void**) &ctx;
#endif /* GC_DEBUG */

  if (--gc_disabled == 0) {
    if (check_gc_request(ctx))
      garbage_collect(ctx);
  }
}

void try_gc(Context *ctx)
{
  if (check_gc_request(ctx))
    garbage_collect(ctx);
}

STATIC void garbage_collect(Context *ctx)
{
  struct rusage ru0, ru1;

  /* printf("Enter gc, generation = %d\n", generation); */
  GCLOG("Before Garbage Collection\n");
  /* print_memory_status(); */
  if (cputime_flag == TRUE) getrusage(RUSAGE_SELF, &ru0);

  scan_roots(ctx);
  weak_clear();
  sweep();
  GCLOG("After Garbage Collection\n");
  /* print_memory_status(); */
  /* print_heap_stat(); */

  if (cputime_flag == TRUE) {
    time_t sec;
    suseconds_t usec;

    getrusage(RUSAGE_SELF, &ru1);
    sec = ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec;
    usec = ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec;
    if (usec < 0) {
      sec--;
      usec += 1000000;
    }
    gc_sec += sec;
    gc_usec += usec;
  }

  generation++;
  /* printf("Exit gc, generation = %d\n", generation); */
}

/*
 * Mark the header
 */
STATIC void mark_cell_header(header_t *hdrp)
{
#ifdef GC_DEBUG
  {
    header_t header  = *hdrp;
    header_t *shadow = get_shadow(hdrp);
    header_t sheader = *shadow;
    assert(HEADER0_GET_MAGIC(header) == HEADER0_MAGIC);
    assert(HEADER0_GET_TYPE(header) == HEADER0_GET_TYPE(sheader));
    assert(HEADER0_GET_SIZE(header) - HEADER0_GET_EXTRA(header) ==
           HEADER0_GET_SIZE(sheader) - HEADER0_GET_EXTRA(sheader));
    assert(HEADER0_GET_GEN(header) == HEADER0_GET_GEN(sheader));
  }
#endif /* GC_DEBUG */
  *hdrp |= GC_MARK_BIT;
}

STATIC void mark_cell(void *ref)
{
  header_t *hdrp = (header_t *)(((uintptr_t) ref) - HEADER_BYTES);
  mark_cell_header(hdrp);
}

STATIC void unmark_cell_header(header_t *hdrp)
{
  *hdrp &= ~GC_MARK_BIT;
}

STATIC int is_marked_cell_header(header_t *hdrp)
{
#if HEADER0_GC_OFFSET <= 4 * 8  /* BITS_IN_INT */
  return *hdrp & GC_MARK_BIT;
#else
  return !!(*hdrp & GC_MARK_BIT);
#endif
}

STATIC int is_marked_cell(void *ref)
{
  header_t *hdrp = (header_t *)(((uintptr_t) ref) - HEADER_BYTES);
  return is_marked_cell_header(hdrp);
}

STATIC int test_and_mark_cell(void *ref)
{
  if (in_js_space(ref)) {
    header_t *hdrp = (header_t *)(((uintptr_t) ref) - HEADER_BYTES);
    if (is_marked_cell_header(hdrp))
      return 1;
    mark_cell_header(hdrp);
  }
  return 0;
}

/*
 * Tracer
 */

STATIC void trace_leaf_object(uintptr_t *ptrp)
{
  uintptr_t ptr = *ptrp;
  if (in_js_space((void *) ptr))
    mark_cell((void *) ptr);
}

STATIC void trace_HashTable(HashTable **ptrp)
{
  HashTable *ptr = *ptrp;

  if (test_and_mark_cell(ptr))
    return;

  if (ptr->body != NULL)
    trace_HashCell_array(&ptr->body, ptr->size);
}

STATIC void trace_HashCell_array(HashCell ***ptrp, uint32_t length)
{
  HashCell **ptr = *ptrp;
  int i;
  if (test_and_mark_cell(ptr))
    return;

  for (i = 0; i < length; i++) {
    if (ptr[i] != NULL)
      trace_HashCell(ptr + i);
  }
}

STATIC void trace_HashCell(HashCell **ptrp)
{
  HashCell *ptr = *ptrp;
  if (test_and_mark_cell(ptr))
    return;

  trace_slot(&ptr->entry.key);
  if (is_transition(ptr->entry.attr))
    trace_HiddenClass((HiddenClass **)&ptr->entry.data);
  if (ptr->next != NULL)
    trace_HashCell(&ptr->next);
}

STATIC void trace_Instruction_array_part(Instruction **ptrp,
                                         size_t n_insns, size_t n_constants)
{
  Instruction *ptr = (Instruction *) *ptrp;
  JSValue *litstart;
  size_t i;
  if (test_and_mark_cell(ptr))
    return;
  litstart = (JSValue *)(&ptr[n_insns]);
  for (i = 0; i < n_constants; i++)
    trace_slot((JSValue *)(&litstart[i]));
}

STATIC void scan_FunctionTable(FunctionTable *ptr)
{
  /* trace constant pool */
  trace_Instruction_array_part(&ptr->insns, ptr->n_insns, ptr->n_constants);
  /* scan Allocation Sites */
  {
    size_t i;
    for (i = 0; i < ptr->n_insns; i++) {
      Instruction *insn = &ptr->insns[i];
      AllocSite *alloc_site = &insn->alloc_site;
      if (alloc_site->hc != NULL)
        trace_HiddenClass(&alloc_site->hc);
      if (alloc_site->preformed_hc != NULL)
        trace_HiddenClass(&alloc_site->preformed_hc);
    }
  }
}

STATIC void trace_FunctionTable_array(FunctionTable **ptrp, size_t length)
{
  FunctionTable *ptr = *ptrp;
  size_t i;
  if (test_and_mark_cell(ptr))
    return;
  for (i = 0; i < length; i++)
    scan_FunctionTable(ptr + i);
}

STATIC void trace_FunctionFrame(FunctionFrame **ptrp)
{
  FunctionFrame *ptr = *ptrp;
  header_t header;
  size_t length;
  size_t   i;
  if (test_and_mark_cell(ptr))
    return;

  if (ptr->prev_frame != NULL)
    trace_FunctionFrame(&ptr->prev_frame);
  trace_slot(&ptr->arguments);
  /* locals */
  header = *(((uint64_t *) ptr) - HEADER_JSVALUES);
  length = HEADER0_GET_SIZE(header);
  length -= HEADER_JSVALUES;
  length -= sizeof(FunctionFrame) >> LOG_BYTES_IN_JSVALUE;
  length -= HEADER0_GET_EXTRA(header);
  for (i = 0; i < length; i++)
    trace_slot(ptr->locals + i);

  assert(ptr->locals[length - 1] == JS_UNDEFINED);  /* GC_DEBUG (cacary) */
}

STATIC void trace_StrCons(StrCons **ptrp)
{
  StrCons *ptr = *ptrp;

  if (test_and_mark_cell(ptr))
    return;

  /* trace_slot(&ptr->str); */ /* weak pointer */
  if (ptr->next != NULL)
    trace_StrCons(&ptr->next);
}

STATIC void trace_StrCons_ptr_array(StrCons ***ptrp, size_t length)
{
  StrCons **ptr = *ptrp;
  size_t i;
  if (test_and_mark_cell(ptr))
    return;

  for (i = 0; i < length; i++)
    if (ptr[i] != NULL)
      trace_StrCons(ptr + i);
}

STATIC void trace_HiddenClass(HiddenClass **ptrp)
{
  HiddenClass *ptr = *ptrp;
  if (test_and_mark_cell(ptr))
    return;
  trace_HashTable(&hidden_map(ptr));
  trace_slot(&hidden_proto(ptr));
  if (hidden_prev(ptr) != NULL)
    trace_HiddenClass(&hidden_prev(ptr));
  if (hidden_base(ptr) != NULL)
    trace_HiddenClass(&hidden_base(ptr));
}

/*
 * we do not move context
 */
STATIC void trace_Context(Context **contextp)
{
  Context *context = *contextp;

  if (test_and_mark_cell(context))
    return;

  trace_slot(&context->global);
  trace_FunctionTable_array(&context->function_table, FUNCTION_TABLE_LIMIT);
  /* TODO: update spregs.cf which is an inner pointer to function_table */
  trace_FunctionFrame(&context->spreg.lp);
  trace_slot(&context->spreg.a);
  trace_slot(&context->spreg.err);

  trace_slot(&context->exhandler_stack);
  trace_slot(&context->lcall_stack);

  /* process stack */
  assert(!is_marked_cell(context->stack));
  mark_cell(context->stack);
  scan_stack(context->stack, context->spreg.sp, context->spreg.fp);
}

STATIC HiddenClass *find_lub(HiddenClass *a, HiddenClass *b)
{
  HiddenClass *p;
  int alen = 0;
  int blen = 0;

  for (p = a; p != NULL; p = p->prev, alen++)
    if (p == b)
      return p;
  for (p = b; p != NULL; p = p->prev, blen++)
    if (p == a)
      return p;
  while (alen > blen) {
    a = a->prev;
    alen--;
  }
  while (alen < blen) {
    b = b->prev;
    blen--;
  }
  while (a != b) {
    a = a->prev;
    b = b->prev;
  }
  return a;
}

STATIC void alloc_site_update_hc(AllocSite *alloc_site, HiddenClass *hc)
{
  alloc_site->hc = hc;
  alloc_site->preformed_hc = NULL;
}

STATIC void trace_js_object(uintptr_t *ptrp)
{
  uintptr_t ptr = *ptrp;
  Object *obj = (Object *) ptr;

  assert(in_js_space((void *) ptr));
  if (is_marked_cell((void *) ptr))
    return;
  mark_cell((void *) ptr);

  /* feedback hidden class statistics to allocation sites. */
  if (obj->alloc_site != NULL) {
    HiddenClass *obj_hc = obj->klass;
    if (hidden_base(obj_hc) != NULL)
      obj_hc = hidden_base(obj_hc);
    if (obj->alloc_site->polymorphic) {
      if (obj->alloc_site->hc != NULL)
        alloc_site_update_hc(obj->alloc_site,
                             find_lub(obj_hc, obj->alloc_site->hc));
    } else {
      if (obj->alloc_site->hc == NULL)
        alloc_site_update_hc(obj->alloc_site, obj_hc);
      else {
        HiddenClass *p;
        /* check monomorphism */
        for (p = obj->alloc_site->hc; p != NULL; p = p->prev)
          if (p == obj_hc)
            break;
        if (p == NULL) {
          for (p = obj_hc; p != NULL; p = p->prev)
            if (p == obj->alloc_site->hc) {
              alloc_site_update_hc(obj->alloc_site, obj_hc);
              break;
            }
        }
        if (p == NULL) {
          obj->alloc_site->polymorphic = 1;
          alloc_site_update_hc(obj->alloc_site,
                               find_lub(obj_hc, obj->alloc_site->hc));
        }
      }
    }
  }

  /* common header */
  trace_HiddenClass(&obj->klass);
  {
    size_t n_limit_props = obj->klass->n_limit_props;
    size_t actual_embedded =
      (n_limit_props == 0) ?
      obj->klass->n_props : (obj->klass->n_embedded_props - 1);
    size_t i;
    for (i = obj->klass->n_special_props; i < actual_embedded; i++)
      trace_slot(&obj->eprop[i]);
    if (n_limit_props != 0) {
      int start = 0;
      if (actual_embedded < obj->klass->n_special_props) {
        /* The last special property is on the overflow array */
        start = 1;
      }
      trace_JSValue_array_range((JSValue **) &obj->eprop[actual_embedded],
                                start, obj->klass->n_props - actual_embedded);
    }
  }

  switch (HEADER0_GET_TYPE(((header_t *) ptr)[-1])) {
  case HTAG_SIMPLE_OBJECT:
    break;
  case HTAG_ARRAY:
    {
      /* If a-> body is NULL, the x-properties of the array has not
       * been initialised.  Note that common fields has been done.
       */
      if (((JSValue**) obj->eprop[ARRAY_XPROP_INDEX_BODY]) != NULL) {
        /* TODO: If an x-property is the last embedded one, it may overflow */
        JSValue **a_body_p = (JSValue **)&obj->eprop[ARRAY_XPROP_INDEX_BODY];
        size_t a_length    = *(size_t *)&obj->eprop[ARRAY_XPROP_INDEX_LENGTH];
        size_t a_size      = *(size_t *)&obj->eprop[ARRAY_XPROP_INDEX_SIZE];
        size_t len = a_length < a_size ? a_length : a_size;
        trace_JSValue_array(a_body_p, len);
      }
    }
    break;
  case HTAG_FUNCTION:
    {
      FunctionTable *ftentry =
        *(FunctionTable **)&obj->eprop[FUNC_XPROP_INDEX_FTENTRY];
      FunctionFrame **frame  =
        (FunctionFrame **)&obj->eprop[FUNC_XPROP_INDEX_ENV];
      scan_FunctionTable(ftentry);
      trace_FunctionFrame(frame);
    }
    break;
  case HTAG_BUILTIN:
    break;
  case HTAG_ITERATOR:
    /* iterator does not have a common header */
    assert(0);
    break;
#ifdef USE_REGEXP
  case HTAG_REGEXP:
    trace_leaf_object((uintptr_t *)&obj->eprop[REX_XPROP_INDEX_PATTERN]);
    break;
#endif /* USE_REGEXP */
  case HTAG_BOXED_STRING:
  case HTAG_BOXED_NUMBER:
  case HTAG_BOXED_BOOLEAN:
    trace_slot(&obj->eprop[BOXED_XPROP_INDEX_VALUE]);
    break;
  default:
    assert(0);
  }
}

STATIC void trace_iterator(Iterator **ptrp)
{
  Iterator *obj = *ptrp;

  assert(in_js_space((void *) obj));
  if (is_marked_cell((void *) obj))
    return;
  mark_cell((void *) obj);
  if (obj->size > 0)
    trace_JSValue_array(&obj->body, obj->size);
}

STATIC void trace_JSValue_array_range(JSValue **ptrp,
                                      size_t start, size_t length)
{
  JSValue *ptr = *ptrp;
  size_t i;

  if (in_js_space(ptr)) {
    if (test_and_mark_cell(ptr))
      return;
  }

  /* SCAN */
  for (i = start; i < length; i++, ptr++)
    trace_slot(ptr);
}

STATIC void trace_slot(JSValue* ptr)
{
  JSValue jsv = *ptr;
  if (is_leaf_object(jsv)) {
    uint8_t tag = jsv & TAGMASK;
    jsv &= ~TAGMASK;
    trace_leaf_object((uintptr_t *) &jsv);
    *ptr = jsv | tag;
  } else if (is_iterator(jsv)) {
    /* iterator does not have common headers but does have pointers */
    uint8_t tag = jsv & TAGMASK;
    jsv &= ~TAGMASK;
    trace_iterator((Iterator **) &jsv);
    *ptr = jsv | tag;
  } else if (is_obj_header_tag((jsv), HTAG_HIDDEN_CLASS)) {
    /* TODO: make hidden class a JSValue */
    trace_HiddenClass((HiddenClass **) &jsv);
    *ptr = jsv;
  } else if (is_pointer(jsv)) {
    uint8_t tag = jsv & TAGMASK;
    jsv &= ~TAGMASK;
    trace_js_object((uintptr_t *) &jsv);
    *ptr = jsv | tag;
  }
}

STATIC void trace_root_pointer(void **ptrp)
{
  void *ptr = *ptrp;

  if ((((uintptr_t) ptr) & TAGMASK) != 0) {
    trace_slot((JSValue *) ptrp);
    return;
  }

  switch (obj_header_tag(ptr)) {
  case HTAG_PROP:
    LOG_EXIT("HTAG_PROP in trace_root_pointer\n"); break;
  case HTAG_ARRAY_DATA:
    LOG_EXIT("HTAG_ARRAY_DATA in trace_root_pointer\n"); break;
  case HTAG_FUNCTION_FRAME:
    trace_FunctionFrame((FunctionFrame **)ptrp); break;
  case HTAG_STR_CONS:
    trace_StrCons((StrCons **)ptrp); break;
  case HTAG_CONTEXT:
    trace_Context((Context **)ptrp); break;
  case HTAG_STACK:
    printf("HTAG_STACK in trace_root_pointer\n"); break;
  case HTAG_HIDDEN_CLASS:
    trace_HiddenClass((HiddenClass **)ptrp); break;
  case HTAG_HASHTABLE:
    trace_HashTable((HashTable **)ptrp); break;
  case HTAG_HASH_BODY:
    LOG_EXIT("HTAG_HASH_BODY in trace_root_pointer\n"); break;
  case HTAG_HASH_CELL:
    LOG_EXIT("HTAG_HASH_CELL in trace_root_pointer\n"); break;
  default:
    trace_slot((JSValue *) ptrp);
    return;
  }
}

STATIC void scan_roots(Context *ctx)
{
  struct global_constant_objects *gconstsp = &gconsts;
  JSValue* p;
  int i;

  /*
   * global variables
   */

  for (p = (JSValue *) gconstsp; p < (JSValue *) (gconstsp + 1); p++) {
    trace_slot(p);
  }

  /*
   * global malloced objects
   * For simplicity, we do not use a `for' loop to visit every object
   * registered in the gobjects.
   */
  trace_HiddenClass(&gobjects.g_hidden_class_top);
  trace_HiddenClass(&gobjects.g_hidden_class_0);
  trace_HiddenClass(&gobjects.g_hidden_class_array);
  trace_HiddenClass(&gobjects.g_hidden_class_function);
  trace_HiddenClass(&gobjects.g_hidden_class_builtin);
  trace_HiddenClass(&gobjects.g_hidden_class_boxed_number);
  trace_HiddenClass(&gobjects.g_hidden_class_boxed_boolean);
  trace_HiddenClass(&gobjects.g_hidden_class_boxed_string);
#ifdef USE_REGEXP
  trace_HiddenClass(&gobjects.g_hidden_class_regexp);
#endif /* USE_REGEXP */

  /* function table: do not trace.
   *                 Used slots should be traced through Function objects
   */
  /* string table */
  trace_StrCons_ptr_array(&string_table.obvector, string_table.size);

  /*
   * Context
   */
  trace_Context(&ctx);

  /*
   * tmp root
   */
  /* old gc root stack */
  for (i = 0; i <= tmp_roots_sp; i++)
    trace_root_pointer((void **) tmp_roots[i]);
  /* new gc root stack */
  for (i = 0; i < gc_root_stack_ptr; i++)
    trace_root_pointer((void **) gc_root_stack[i]);
}

STATIC void scan_stack(JSValue* stack, int sp, int fp)
{
  while (1) {
    while (sp >= fp) {
      trace_slot(stack + sp);
      sp--;
    }
    if (sp < 0)
      return;
    fp = stack[sp--];                                     /* FP */
    trace_FunctionFrame((FunctionFrame **)(stack + sp));  /* LP */
    sp--;
    sp--;                                                 /* PC */
    scan_FunctionTable((FunctionTable *) stack[sp--]);    /* CF */
    /* TODO: fixup inner pointer (CF) */
  }
}


/*
 * Clear pointer field to StringCell whose mark bit is not set.
 * Unlink the StrCons from the string table.  These StrCons's
 * are collected in the next collection cycle.
 */
STATIC void weak_clear_StrTable(StrTable *table)
{
  size_t i;
  for (i = 0; i < table->size; i++) {
    StrCons ** p = table->obvector + i;
    while (*p != NULL) {
      StringCell *cell = remove_normal_string_tag((*p)->str);
      if (!is_marked_cell(cell)) {
        (*p)->str = JS_UNDEFINED;
        *p = (*p)->next;
      } else
        p = &(*p)->next;
    }
  }
}

STATIC void weak_clear(void)
{
  weak_clear_StrTable(&string_table);
}

STATIC void sweep_space(struct space *space)
{
  struct free_chunk **p;
  uintptr_t scan = space->addr;
  uintptr_t free_bytes = 0;

  GCLOG_SWEEP("sweep %s\n", space->name);

  space->freelist = NULL;
  p = &space->freelist;
  while (scan < space->addr + space->bytes) {
    uintptr_t last_used = 0;
    uintptr_t free_start;
    /* scan used area */
    while (scan < space->addr + space->bytes &&
           is_marked_cell_header((void *) scan)) {
      header_t header = *(header_t *) scan;
      size_t size = HEADER0_GET_SIZE(header);
#ifdef GC_DEBUG
      assert(HEADER0_GET_MAGIC(header) == HEADER0_MAGIC);
#endif /* GC_DEBUG */
      unmark_cell_header((void *) scan);
      last_used = scan;
      scan += size << LOG_BYTES_IN_JSVALUE;
    }
    free_start = scan;
    while (scan < space->addr + space->bytes &&
           !is_marked_cell_header((void *) scan)) {
      uint64_t header = *(uint64_t *) scan;
      uint32_t size = HEADER0_GET_SIZE(header);
#ifdef GC_DEBUG
      assert(HEADER0_GET_MAGIC(header) == HEADER0_MAGIC);
#endif /* GC_DEBUG */
      scan += size << LOG_BYTES_IN_JSVALUE;
    }
    if (free_start < scan) {
      if (last_used != 0) {
        uint64_t last_header = *(uint64_t *) last_used;
        uint32_t extra = HEADER0_GET_EXTRA(last_header);
        uint32_t size = HEADER0_GET_SIZE(last_header);
        free_start -= extra << LOG_BYTES_IN_JSVALUE;
        size -= extra;
        HEADER0_SET_SIZE(*(uint64_t *) last_used, size);
        HEADER0_SET_EXTRA(*(uint64_t *) last_used, 0);
      }
      if (scan - free_start >=
          MINIMUM_FREE_CHUNK_JSVALUES << LOG_BYTES_IN_JSVALUE) {
        struct free_chunk *chunk = (struct free_chunk *) free_start;
        GCLOG_SWEEP("add_cunk %x - %x (%d)\n",
                    free_start - space->addr, scan - space->addr,
                    scan - free_start);
#ifdef GC_DEBUG
        memset(chunk, 0xcc, scan - free_start);
#endif /* GC_DEBUG */
        chunk->header =
          HEADER0_COMPOSE((scan - free_start) >> LOG_BYTES_IN_JSVALUE,
                          0, HTAG_FREE);
#ifdef GC_DEBUG
        HEADER0_SET_MAGIC(chunk->header, HEADER0_MAGIC);
#endif /* GC_DEBUG */
        *p = chunk;
        p = &chunk->next;
        free_bytes += scan - free_start;
      } else  {
        *(header_t *) free_start =
          HEADER0_COMPOSE((scan - free_start) >> LOG_BYTES_IN_JSVALUE,
                          0, HTAG_FREE);
#ifdef GC_DEBUG
        HEADER0_SET_MAGIC(*(header_t *) free_start, HEADER0_MAGIC);
#endif /* GC_DEBUG */
      }
    }
  }
  (*p) = NULL;
  space->free_bytes = free_bytes;
}


STATIC void sweep(void)
{
#ifdef GC_DEBUG
  sanity_check();
  check_invariant();
#endif /* GC_DEBUG */
  sweep_space(&js_space);
}

#ifdef GC_DEBUG
STATIC void check_invariant_nobw_space(struct space *space)
{
  uintptr_t scan = space->addr;

  while (scan < space->addr + space->bytes) {
    header_t *hdrp = (header_t *) scan;
    header_t header = *hdrp;
    switch (HEADER0_GET_TYPE(header)) {
    case HTAG_STRING:
    case HTAG_FLONUM:
    case HTAG_ARRAY_DATA:
    case HTAG_CONTEXT:
    case HTAG_STACK:
    case HTAG_HIDDEN_CLASS:
    case HTAG_HASHTABLE:
    case HTAG_HASH_CELL:
      break;
    default:
      if (is_marked_cell_header(hdrp)) {
        /* this object is black; should not contain a pointer to white */
        size_t payload_jsvalues = HEADER0_GET_SIZE(header);
        size_t i;
        payload_jsvalues -= HEADER_JSVALUES;
        payload_jsvalues -= HEADER0_GET_EXTRA(header);
        for (i = 0; i < payload_jsvalues; i++) {
          uintptr_t x = ((uintptr_t *) (scan + BYTES_IN_JSVALUE))[i];
          if (HEADER0_GET_TYPE(header) == HTAG_STR_CONS) {
            if (i ==
                (((uintptr_t) &((StrCons *) 0)->str) >> LOG_BYTES_IN_JSVALUE))
              continue;
          }
          if (in_js_space((void *)(x & ~7))) {
            assert(is_marked_cell((void *) (x & ~7)));
          }
        }
      }
      break;
    }
    scan += HEADER0_GET_SIZE(header) << LOG_BYTES_IN_JSVALUE;
  }
}

STATIC void check_invariant(void)
{
  check_invariant_nobw_space(&js_space);
}


STATIC void print_memory_status(void)
{
  GCLOG("  gc_disabled = %d\n", gc_disabled);
  GCLOG("  js_space.free_bytes = %d\n", js_space.free_bytes);
}

STATIC void print_heap_stat(void)
{
  size_t jsvalues[17] = {0, };
  size_t number[17] = {0, };
  uintptr_t scan = js_space.addr;
  size_t i;

  while (scan < js_space.addr + js_space.bytes) {
    header_t header = *(header_t *) scan;
    cell_type_t type = HEADER0_GET_TYPE(header);
    size_t size = HEADER0_GET_SIZE(header);
    if (type != HTAG_FREE) {
      jsvalues[type] += size;
      number[type] ++;
    }
    scan += (size << LOG_BYTES_IN_JSVALUE);
  }

  for (i = 0; i < 17; i++) {
    printf("type %02zu: num = %08zu volume = %08zu\n", i, number[i], jsvalues[i]);
  }
}

extern void** stack_start;
STATIC void sanity_check()
{
}
#endif /* GC_DEBUG */

/* Local Variables:      */
/* mode: c               */
/* c-basic-offset: 2     */
/* indent-tabs-mode: nil */
/* End:                  */
