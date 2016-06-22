//
//  codeloader.c
//  SSJSVM Project, iwasaki-lab, UEC
//
//  Sho Takada, 2012-13
//  Akira Tanimura, 2012-13
//  Akihiro Urushihara, 2013-14
//  Hideya Iwasaki, 2013-16
//  Ryota Fujii, 2013-14
//

#include "prefix.h"
#define EXTERN extern
#include "header.h"

// information of instructions
//
static InsnInfo insn_info_table[] = {
#include "instructions-table.h"
};

char *insn_nemonic(int opcode) {
  return insn_info_table[opcode].insn_name;
}

// instruction table
//
typedef struct insn_cons {
  int opcode;
  struct insn_cons *next;  // pointer to the next instCons
} InsnCons;

#define INSN_HASH_SIZE (1000)

static InsnCons *insn_hash_table[INSN_HASH_SIZE];

#define LOADBUFLEN 1024

extern uint32_t decode_escape_char(char *);
extern int print_function_table(FunctionTable *, int);
extern void print_bytecode(Instruction *, int);

// int number_functions;
FILE *filePointer;

// reads the next line from the input stream
//
inline void step_load_code(char *buf) {
  if (filePointer != NULL) {
    fgets(buf, LOADBUFLEN, filePointer);
  } else {
    fgets(buf, LOADBUFLEN, stdin);
  }
}

#define DELIM " \n\r"
#define DELIM2 "\n\r"
#define FirstToken(b) strtok(b, DELIM)
#define NextToken()   strtok(NULL, DELIM)
#define NextToken2()  strtok(NULL, DELIM2)

inline int check_read_token(char *buf, char *tok) {
  char *p;
  p = FirstToken(buf);
  if (strcmp(p, tok) != 0) {
    LOG_EXIT("Error: %s is not defined", tok);
  }
  return atoi(NextToken());
}

// codeloader
//
int code_loader(FunctionTable *ftable) {
  ConstantCell ctable;
  Bytecode* bytecodes;
  char buf[LOADBUFLEN];
  int nfuncs, callentry, sendentry, nlocals, ninsns;
  int i, j, ret;

  // checks the funcLength and optain the number of functions
  step_load_code(buf);
  nfuncs = check_read_token(buf, "funcLength");

  //LOG("numberOfFunc: %d\n",numberOfFunction);

  // reads each function
  for (i = 0; i < nfuncs; i++) {
    // callentry
    step_load_code(buf);
    callentry = check_read_token(buf, "callentry");

    // sendentry
    step_load_code(buf);
    sendentry = check_read_token(buf, "sendentry");

    // numberOfLocals
    step_load_code(buf);
    nlocals = check_read_token(buf, "numberOfLocals");

    // numberOfInstruction
    step_load_code(buf);
    ninsns = check_read_token(buf, "numberOfInstruction");

    // initializes constant table
    init_constant_cell(&ctable);

    // initilaizes bytecode array
    bytecodes = (Bytecode*)malloc(sizeof(Bytecode) * ninsns);
    if (bytecodes == NULL) {
      LOG_EXIT("%dth func: cannnot malloc bytecode", i);
    }

    // loads instructions for each function
    for (j = 0; j < ninsns; j++) {
      ret = insn_load(&ctable, bytecodes, j);
    }

    ret = update_function_table(ftable, i, &ctable, bytecodes,
                               callentry, sendentry, nlocals, ninsns);
    end_constant_cell(&ctable);
  }
  // number_functions = i;

  print_function_table(ftable, i);

  return i;
}

// initializes the code loader
//
void init_code_loader(FILE *fp) {
  int i;
  int numinsts;
  uint32_t index;
  char *name;
  InsnCons *c;

  for (i = 0; i < INSN_HASH_SIZE; i++)
    insn_hash_table[i] = NULL;

  // register every instruction names and its opcode
  numinsts = sizeof(insn_info_table) / sizeof(InsnInfo);
  for (i = 0; i < numinsts; i++) {
    name = insn_info_table[i].insn_name;
    index = calc_hash(name) % INSN_HASH_SIZE;
    c = (InsnCons *) malloc(sizeof(InsnCons));
    c->opcode = i;
    c->next = insn_hash_table[index];
    insn_hash_table[index] = c;
  }
  filePointer = fp;
}

// finalizes the code loader
//
void end_code_loader() {
  if (filePointer != NULL) {
    fclose(filePointer);
  }
}

#define NOT_OPCODE ((Opcode)(-1))

Opcode find_insn(char* s) {
  int index, oc;
  InsnCons *c;

  index = calc_hash(s) % INSN_HASH_SIZE;
  for (c = insn_hash_table[index]; c != NULL; c = c->next) {
    oc = c->opcode;
    if (strcmp(insn_info_table[oc].insn_name, s) == 0) return oc;
  }
  // not found in the instruction table
  return NOT_OPCODE;
}

static char *null_string = "";

#define LOAD_OK     0
#define LOAD_FAIL  (-1)

// loads an instruction
//
int insn_load(ConstantCell *constant, Bytecode *bytecodes, int pc) {
  char buf[LOADBUFLEN];
  char *tokPointer;
  Opcode oc;

  step_load_code(buf);
  tokPointer = FirstToken(buf);
  oc = find_insn(tokPointer);
  if (oc == NOT_OPCODE) {
    // instruction is not found in the instruction info table
    LOG_ERR("Illegal instruction: %s", tokPointer);
    bytecodes[pc] = (Bytecode)(-1);
    return LOAD_FAIL;
  }
  switch (insn_info_table[oc].operand_type) {
  case SMALLPRIMITIVE:
    {
      Register dst;
      dst = atoi(NextToken());
      switch (oc) {
      case FIXNUM:
        bytecodes[pc] = makecode_fixnum(dst, atoi(NextToken()));
        break;
      case SPECCONST:
        bytecodes[pc] =
          makecode_specconst(dst, specstr_to_jsvalue(NextToken()));
        break;
      default:
        return LOAD_FAIL;
      }
      return LOAD_OK;
    }

  case BIGPRIMITIVE:
    {
      Register dst;
      dst = atoi(NextToken());
      switch (oc) {
      case NUMBER:
        {
          double number;
          int index;
          number = atof(NextToken());
          // writes the number into the constant table
          index = add_constant_number(constant, number);
          bytecodes[pc] = makecode_number(dst, index);
        }
        break;
      case STRING:
      case ERROR:
        {
          char* str;
          int index;
          uint32_t len;
          str = NextToken2();
          if (str == NULL) str = null_string;
          else len = decode_escape_char(str);
          // printf("  decoded string = %s\n", str);
          index = add_constant_string(constant, str);
          if (oc == STRING)
            bytecodes[pc] = makecode_string(dst, index);
          else
            bytecodes[pc] = makecode_error(dst, index);
        }
        break;
#ifdef USE_REGEXP
      case REGEXP:
        {
          char* str;
          int index;
          unsigned int flag;
          flag = atoi(NextToken());
          str = NextToken();
          index = add_constant_regexp(constant, str, flag);
          bytecodes[pc] = makecode_regexp(dst, index, flag);
        }
        break;
#endif
      default:
        return LOAD_FAIL;
      }
      return LOAD_OK;
    }

  case THREEOP:
    {
      Register op1, op2, op3;
      op1 = atoi(NextToken());
      op2 = atoi(NextToken());
      op3 = atoi(NextToken());
      bytecodes[pc] = makecode_three_operands(oc, op1, op2, op3);
      return LOAD_OK;
    }

  case TWOOP:
    {
      Register op1, op2;
      op1 = atoi(NextToken());
      op2 = atoi(NextToken());
      bytecodes[pc] = makecode_two_operands(oc, op1, op2);
      return LOAD_OK;
    }

  case ONEOP:
    {
      Register op;
      op = atoi(NextToken());
      bytecodes[pc] = makecode_one_operand(oc, op);
      return LOAD_OK;
    }

  case ZEROOP:
    {
      bytecodes[pc] = makecode_no_operand(oc);
      return LOAD_OK;
    }

  case UNCONDJUMP:
    {
      Displacement disp;
      disp = (Displacement)atoi(NextToken());
      bytecodes[pc] = makecode_jump(disp);
      return LOAD_OK;
    }

  case CONDJUMP:
    {
      Displacement disp;
      Register src;
      src = atoi(NextToken());
      disp = (Displacement)atoi(NextToken());
      bytecodes[pc] = makecode_cond_jump(oc, src, disp);
      return LOAD_OK;
    }

  case GETVAR:
    {
      Subscript link, offset;
      Register reg;
      link = atoi(NextToken());
      offset = atoi(NextToken());
      reg = atoi(NextToken());
      bytecodes[pc] = makecode_getvar(oc, link, offset, reg);
      return LOAD_OK;
    }

  case SETVAR:
    {
      Subscript link, offset;
      Register reg;
      link = atoi(NextToken());
      offset = atoi(NextToken());
      reg = atoi(NextToken());
      bytecodes[pc] = makecode_setvar(oc, link, offset, reg);
      return LOAD_OK;
    }

  case MAKECLOSUREOP:
    {
      Register dst;
      uint16_t index;
      dst = atoi(NextToken());
      index = (uint16_t)atoi(NextToken());
      bytecodes[pc] = makecode_makeclosure(oc, dst, index);
      return LOAD_OK;
    }

  case CALLOP:
    {
      Register closure;
      uint16_t argc;
      closure = atoi(NextToken());
      argc = atoi(NextToken());
printf("CALLOP: argc = %d\n", argc);
      bytecodes[pc] = makecode_call(oc, closure, argc);
      return LOAD_OK;
    }

  case TRYOP:
    {
      Displacement disp;
      disp = (Displacement)atoi(NextToken());
      bytecodes[pc] = makecode_try(disp);
      return LOAD_OK;
    }

  default:
    {
      LOG_EXIT("Illegal instruction: %s\n", tokPointer);
      return LOAD_FAIL;
    }
  }
}

// initilizes the contant table
// 
void init_constant_cell(ConstantCell *constant)
{
  constant->n_constant_values = 0;
  constant->constant_values =
    (JSValue*)malloc(sizeof(JSValue) * (CONSTANT_LIMIT));
}

// finlaizes the constant table
//
void end_constant_cell(ConstantCell *constant){
  // do nothing
}

// converts a special JS string (for a constant) into a JSValue
//
JSValue specstr_to_jsvalue(const char *str) {
  if (strcmp(str, "true") == 0) {
    return JS_TRUE;
  } else if (strcmp(str, "false") == 0) {
    return JS_FALSE;
  } else if (strcmp(str, "null") == 0) {
    return JS_NULL;
  } else if (strcmp(str, "undefined") == 0) {
    return JS_UNDEFINED;
  } else {
    // undefined name
    LOG_EXIT("%s is an undefined symbol.", str);
  }
}

char *jsvalue_to_specstr(JSValue v) {
  char *s;
  if (v == JS_TRUE) s = "true";
  else if (v == JS_FALSE) s = "false";
  else if (v == JS_NULL) s = "null";
  else if (v == JS_UNDEFINED) s = "undefined";
  else s = "unknown_specstr";
  return s;
}

// add_constant_number
// adds a number into the constant table.
// FIXME:
// Currently, the same constant numbers might be added to the table
// more than once.  It might be better to avoid such duplication.
//
int add_constant_number(ConstantCell *constant, double x) {
  int index;

  index = constant->n_constant_values++;
  (constant->constant_values)[index] = double_to_number(x);
  return index;
}

// add_constant_string
// adds a string into the constant table.
//
int add_constant_string(ConstantCell *constant, char *str) {
  JSValue a;
  int index;

  index = constant->n_constant_values++;
  a = cstr_to_string(str);
  // printf("updateConstantString: str = %s, a = %lld (%s)\n",
  //        str, a, stringToCStr(a));
  (constant->constant_values)[index] = a;
  return index;
}

// add_constant_regexp
// adds a regexp into the constant table.
// FIXME: This code has also the ``duplication'' problem.
//
#ifdef USE_REGEXP
int add_constant_regexp(ConstantCell *constant, char *str, int flag) {
  JSValue a;
  int index;
  OnigOptionType option;

  index = constant->n_constant_values++;
  a = new_regexp();
  get_regexp_pattern(a) = ststrdup(str);
  option = set_regexp_flag(flag, a);
  if (!make_onig_regexp(a, option) == MAKE_REGEX_OBJECT_SUCCESS) {
    LOG_ERR("an error occured in making regex.");
  }
  (constant->constant_values)[index] = a;
  return index;
}
#endif

#ifdef USE_REGEXP
inline OnigOptionType setRegExpFlag(int flag, JSValue regex) {
  OnigOptionType onigOption = ONIG_OPTION_NONE;

  if (flag & F_REGEXP_GLOBAL) {
    setRegExpGlobal(regex, true);
    setObjProp(regex, "global", JS_TRUE, ATTR_ALL);
  } else {
    setObjProp(regex, "global", JS_FALSE, ATTR_ALL);
  }

  if (flag & F_REGEXP_IGNORE) {
    onigOption |= ONIG_OPTION_IGNORECASE;
    setRegExpIgnoreCase(regex, true);
    setObjProp(regex, "ignoreCase", JS_TRUE, ATTR_ALL);
  } else {
    setObjProp(regex, "ignoreCase", JS_FALSE, ATTR_ALL);
  }

  if (flag & F_REGEXP_MULTILINE) {
    onigOption |= ONIG_OPTION_MULTILINE;
    setRegExpMultiline(regex, true);
    setObjProp(regex, "multiline", JS_TRUE, ATTR_ALL);
  } else {
    setObjProp(regex, "multiline", JS_FALSE, ATTR_ALL);
  }
  return onigOption;
}
#endif

int update_function_table(FunctionTable *ftable, int index,
                          ConstantCell *constant, Bytecode *bytecodes,
                          int callentry, int sendentry,
                          int nlocals, int ninsns) {
  int i;
  Opcode oc;
  Instruction *insns;
  void **insnptr;
  int bodysize;

#ifdef J5MODE
  if (index == 0) {
    ninsns++;
  }
#endif

  bodysize = ninsns + constant->n_constant_values;
  insns = (Instruction *)malloc(sizeof(Instruction) * bodysize);
  // The above shold be the following???
  // insns = (Instruction *)malloc(sizeof(Instruction) * ninsns +
  //                            sizeof(JSvalue) * constant->n_constant_values);
  insnptr = (void **)malloc(sizeof(void *) * ninsns);
  int loopnum = ninsns;

#ifdef J5MODE
  loopnum += (index == 0)? -1: 0;
#endif

  // rewrites the operand of STRING/NUMBER/REGEXP instraction
  // as a relative displacement
  for (i = 0; i < loopnum; i++) {
    insnptr[i] = NULL;
    oc = get_opcode(bytecodes[i]);
    if (oc == STRING || oc == NUMBER || oc == REGEXP) {
      Subscript ss;
      Displacement disp;
      ss = get_big_subscr(bytecodes[i]);
      disp = calc_displacement(ninsns, i, ss);
      bytecodes[i] = update_displacement(bytecodes[i], disp);
    }
  }

  // fills the insns array
  for (i = 0; i < loopnum; i++) {
    insns[i].code = bytecodes[i];
#ifdef USE_THRESHOLD
    insns[i].hitCount = 0;
    insns[i].missCount = 0;
#endif
#ifdef PRINT_QUICKENING_COUNT
    insns[i].quickeningCount = 0;
    insns[i].executeCount = 0;
#endif
  }

  // writes the constants
  for (; i < bodysize; i++) {
    insns[i].code = constant->constant_values[i - ninsns];
  }

  ftable[index].insn_ptr_created = false;
  ftable[index].insns = insns;
  ftable[index].insn_ptr = insnptr;
  ftable[index].call_entry = callentry;
  ftable[index].send_entry = sendentry;
  ftable[index].n_locals = nlocals;
  ftable[index].body_size = bodysize;
  ftable[index].n_insns = ninsns;

#ifdef USE_JIT
  ftable[indexOfFuncTbl].jitCount = 0;
  ftable[indexOfFuncTbl].jitList = NULL;
#endif

  return 0;
}

int print_function_table(FunctionTable *ftable, int nfuncs) {
  int i, j;

  printf("number of functions = %d\n", nfuncs);
  for (i = 0; i < nfuncs; i++) {
    printf("function #%d\n", i);
    printf("call_entry: %d\n", ftable[i].call_entry);
    printf("send_entry: %d\n", ftable[i].send_entry);
    printf("n_locals: %d\n" ,ftable[i].n_locals);
    printf("n_insns: %d\n", ftable[i].n_insns);
    printf("body_size: %d\n", ftable[i].body_size);
#ifdef PARALLEL
    printf("bytecode(normal):\n");
#endif
    for (j = 0; j < ftable[i].n_insns; j++) {
      printf("%03d: %016lx --- ", j, ftable[i].insns[j].code);
      print_bytecode(ftable[i].insns, j);
    }
    for (; j < ftable[i].body_size; j++) {
      JSValue o;
      int tag;
      printf("%03d: %016lx --- ", j, ftable[i].insns[j].code);
      o = ftable[i].insns[j].code;
      tag = get_tag(o);
      if (tag == T_FLONUM)
        printf("FLONUM %f\n",
               ((FlonumCell *)remove_tag(o, T_FLONUM))->value);
      else if (tag == T_STRING)
        printf("STRING \"%s\"\n",
               ((StringCell *)remove_tag(o, T_STRING))->value);
#ifdef USE_REGEXP
      else if (tag == T_REGEXP)
        printf("REGEXP \"%s\"\n",
               ((RegexpCell *)remove_tag(o, T_REGEXP))->pattern);
#endif
      else
        printf("Unexpected JSValue\n");
    }
#ifdef PARALLEL
    printf("bytecode(parallel):\n");
    for (j = 0; j < ftable[i].body_size; j++)
      printf("%03d:%016lx\n", j, ftable[i].parallelInsns[j].code);
#endif
  }
  return 0;
}

// prints a bytecode instruction
//
void print_bytecode(Instruction *insns, int j) {
  Bytecode code;
  Opcode oc;
  OperandType t;

  code = insns[j].code;
  oc = get_opcode(code);
  t = insn_info_table[oc].operand_type;
  printf("%s ", insn_info_table[oc].insn_name);
  switch (t) {
  case SMALLPRIMITIVE:
    {
      Register dst;
      int imm;
      dst = get_first_operand_reg(code);
      switch (oc) {
      case FIXNUM:
        imm = get_small_immediate(code);
        printf("%d %d", dst, imm);
        break;
      case SPECCONST:
        imm = get_small_immediate(code);
        printf("%d %s", dst, jsvalue_to_specstr(imm));
        break;
      default:
        printf("???");
        break;
      }
    }
    break;
  case BIGPRIMITIVE:
    {
      Register dst;
      uint16_t disp;
      JSValue o;
      int tag;
      dst = get_first_operand_reg(code);
      disp = get_big_disp(code);
      o = (JSValue)(insns[j + disp].code);
      // printf("j = %d, disp = %d, o = %p\n", j, disp, (char *)o);
      tag = get_tag(o);
      switch (oc) {
      case NUMBER:
        if (tag == T_FLONUM)
          printf("%d %f", dst, ((FlonumCell *)remove_tag(o, T_FLONUM))->value);
        else
          printf("Illegal_object_with_tag_%d", tag);
        break;
      case STRING:
      case ERROR:
        if (tag == T_STRING)
          printf("%d \"%s\"", dst, ((StringCell *)remove_tag(o, T_STRING))->value);
        else
          printf("Illegal_object_with_tag_%d", tag);
        break;
#ifdef USE_REGEXP
      case REGEXP:
        if (tag == T_REGEXP)
          printf("%d \"%s\"", dst, ((RegexpCell *)remove_tag(o, T_REGEXP))->pattern);
        else
          printf("Illegal_object_with_tag_%d", tag);
        break;
#endif // USE_REGEXP
      default:
        printf("???");
        break;
      }
    }
    break;
  case THREEOP:
    {
      Register dst, r1, r2;
      dst = get_first_operand_reg(code);
      r1 = get_second_operand_reg(code);
      r2 = get_third_operand_reg(code);
      printf("%d %d %d", dst, r1, r2);
    }
    break;
  case TWOOP:
    {
      Register dst, r1;
      dst = get_first_operand_reg(code);
      r1 = get_second_operand_reg(code);
      printf("%d %d", dst, r1);
    }
    break;
  case ONEOP:
    {
      Register dst;
      dst = get_first_operand_reg(code);
      printf("%d", dst);
    }
    break;
  case ZEROOP:
    break;
  case UNCONDJUMP:
  case TRYOP:
    {
      Displacement disp;
      disp = get_first_operand_disp(code);
      printf("%d", j + disp);
    }
    break;
  case CONDJUMP:          
    {
      Register r;
      Displacement disp;
      r = get_first_operand_reg(code);
      disp = get_second_operand_disp(code);
      printf("%d %d", r, j + disp);
    }
    break;
  case GETVAR:
    {
      Register dst;
      Subscript link, ss;
      link = get_first_operand_subscr(code);
      ss = get_second_operand_subscr(code);
      dst = get_third_operand_reg(code);
      printf("%d %d %d", link, ss, dst);
    }
    break;
  case SETVAR:
    {
      Register src;
      Subscript link, ss;
      link = get_first_operand_subscr(code);
      ss = get_second_operand_subscr(code);
      src = get_third_operand_reg(code);
      printf("%d %d %d", link, ss, src);
    }
    break;
  case MAKECLOSUREOP:
    {
      Register dst;
      Subscript ss;
      dst = get_first_operand_reg(code);
      ss = get_second_operand_subscr(code);
      printf("%d %d", dst, ss);
    }
    break;
  case CALLOP:
    {
      Register f;
      int na;
      f = get_first_operand_reg(code);
      na = get_second_operand_int(code);
      printf("%d %d", f, na);
    }
    break;
  case UNKNOWNOP:
    break;
  }
  putchar('\n');
}

uint32_t decode_escape_char(char *str) {
  char *src, *dst;
  int dq;
  char c;

  src = dst = str;
  dq = 0;
  if ((c = *src++) == '\"') {
    dq = 1;
    c = *src++;
  }
  while (1) {
    if (dq == 1 && c == '\"') break;
    if (dq == 0 && c == '\0') break;
    if (c != '\\') {
      *dst++ = c;
      c = *src++;
      continue;
    }
    switch (c = *src++) {
    case '0': *dst++ = '\0'; break;
    case 'a': *dst++ = '\a'; break;
    case 'b': *dst++ = '\b'; break;
    case 'f': *dst++ = '\f'; break;
    case 'n': *dst++ = '\n'; break;
    case 'r': *dst++ = '\r'; break;
    case 't': *dst++ = '\t'; break;
    case 'v': *dst++ = '\v'; break;
    case '\\': *dst++ = '\\'; break;
    case '\'': *dst++ = '\''; break;
    case '\"': *dst++ = '\"'; break;
    case 'x':
      {
        int k = 0, i;
        for (i = 0; i <= 1; i++) {
          c = *src++;
          if (c == ' ') c = '0';
          k <<= 4;
          if ('0' <= c && c <= '9') k += c - '0';
          else if ('a' <= c && c <= 'f') k += c + 10 - 'a';
          else if ('A' <= c && c <= 'F') k += c + 10 - 'A';
        }
        *dst++ = (char)k;
      }
      break;
    default: *dst++ = c; break;
    }
    c = *src++;
  }
  *dst = '\0';
  return (uint32_t)(dst - str);
}

#ifdef PARALLEL
#define REWRITE(code, org, new) \
  case org: \
    code = (((Bytecode)new << OPCODE_OFFSET) | ((Bytecode)(code) & ~OPCODE_MASK)); \
    break

// For all functions, rewrite instructions to their parallel versions.
//
void generateParallelCode(FunctionTableCell *ftable, int nfuncs)
{
  int i, j;

  for (i = 0; i < nfuncs; ++i) {
    FunctionTableCell *currentFunc = &ftable[i];
    currentFunc->parallelInsns = (Instruction *)malloc(sizeof(Instruction) * currentFunc->bodysize);
    if (!(currentFunc->parallelInsns))
      LOG_EXIT("%dth func: cannnot malloc bytecode for parallel execution\n", i);

    for (j = 0; j < currentFunc->n_insns; ++j) {
      instruction *instOrg = &(currentFunc->insns[j]);
      instruction *instPar = &(currentFunc->parallelInsns[j]);

      instPar->code = instOrg->code;
      switch (get_opcode(instPar->code)) {
      REWRITE(instPar->code, GETGLOBAL, GETGLOBALPAR);
      REWRITE(instPar->code, SETGLOBAL, SETGLOBALPAR);
      REWRITE(instPar->code, GETPROP, GETPROPPAR);
      REWRITE(instPar->code, SETPROP, SETPROPPAR);
      REWRITE(instPar->code, GETARG, GETARGPAR);
      REWRITE(instPar->code, GETLOCAL, GETLOCALPAR);
      REWRITE(instPar->code, SETARG, SETARGPAR);
      REWRITE(instPar->code, SETLOCAL, SETLOCALPAR);
      REWRITE(instPar->code, CALL, CALLPAR);
      REWRITE(instPar->code, SEND, SENDPAR);
      REWRITE(instPar->code, RET, RETPAR);
      default: break;
      }
    }

    for (; j < currentFunc->body_size; ++j)
      currentFunc->parallelInsns[j].code = currentFunc->insns[j].code;
  }
}
#endif
