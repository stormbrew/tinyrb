#include <stdio.h>
#include <assert.h>
#include "tr.h"
#include "opcode.h"
#include "internal.h"

OBJ TrVM_step(VM);

static void TrFrame_push(VM, TrBlock *b, OBJ self, OBJ class) {
  vm->cf++;
  if (vm->cf > TR_MAX_FRAMES) tr_raise("Stack overflow");
  TrFrame *f = FRAME;
  f->block = b;
  f->method = TR_NIL;
  f->regs = TR_ALLOC_N(OBJ, b->regc);
  f->locals = TR_ALLOC_N(OBJ, kv_size(b->locals));
  f->self = self;
  f->class = class;
  f->line = 1;
  f->ip = b->code.a;
}

static void TrFrame_pop(VM) {
  vm->cf--;
}

static inline OBJ TrVM_lookup(VM, TrFrame *f, OBJ receiver, OBJ msg, TrInst *ip) {
  OBJ method = TrObject_method(vm, receiver, msg);
  if (!method) tr_raise("Method not found: %s\n", TR_STR_PTR(msg));
  TrInst *boing = (ip-1);

#ifdef TR_CALL_SITE
  TrCallSite *s = (kv_pushp(TrCallSite, f->block->sites));
  /* TODO support metaclass */
  s->class = TR_COBJECT(receiver)->class;
  s->method = method;
  s->miss = 0;
  
#ifdef TR_INLINE_METHOD
  #define DEF_INLINE(F, OP) \
    if (func == (TrFunc*)(F)) { \
      boing->i = TR_OP_##OP; \
      boing->a = ip->a; \
      boing->b = 2; \
      return method; \
    }
  TrFunc *func = TR_CMETHOD(method)->func;
  /* try to inline the method as an instruction if possible */
  DEF_INLINE(TrFixnum_add, FIXNUM_ADD)
  else
  DEF_INLINE(TrFixnum_sub, FIXNUM_SUB)
  else
  DEF_INLINE(TrFixnum_lt, FIXNUM_LT)
  #undef DEF_INLINE
#endif

  /* Implement Monomorphic method cache by replacing the previous instruction (BOING)
     w/ CACHE that uses the CallSite to find the method instead of doing a full lookup. */
  boing->i = TR_OP_CACHE;
  boing->a = ip->a; /* receiver register */
  boing->b = 1; /* jmp */
  boing->c = kv_size(f->block->sites)-1; /* CallSite index */
#endif
  
  return method;
}

static inline OBJ TrVM_call(VM, TrFrame *f, OBJ receiver, OBJ method, int argc, OBJ *args) {
  f->method = TR_CMETHOD(method);
  if (f->method->arity == -1) {
    return f->method->func(vm, receiver, argc, args);
  } else {
    if (f->method->arity != argc) tr_raise("Expected %d arguments, got %d.\n", f->method->arity, argc);
    switch (argc) {
      case 0:  return f->method->func(vm, receiver); break;
      case 1:  return f->method->func(vm, receiver, args[0]); break;
      case 2:  return f->method->func(vm, receiver, args[0], args[1]); break;
      case 3:  return f->method->func(vm, receiver, args[0], args[1], args[2]); break;
      case 4:  return f->method->func(vm, receiver, args[0], args[1], args[2], args[3]); break;
      case 5:  return f->method->func(vm, receiver, args[0], args[1], args[2], args[3], args[4]); break;
      case 6:  return f->method->func(vm, receiver, args[0], args[1], args[2], args[3], args[4], args[5]); break;
      case 7:  return f->method->func(vm, receiver, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break;
      case 8:  return f->method->func(vm, receiver, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break;
      case 9:  return f->method->func(vm, receiver, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break;
      case 10: return f->method->func(vm, receiver, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break;
      default: tr_raise("Too much arguments: %d, max is %d for now.\n", argc, 10);
    }
  }
}

static inline OBJ TrVM_defclass(VM, TrFrame *f, OBJ name, TrBlock *b) {
  OBJ class = TrObject_const_get(vm, FRAME->class, name);
  
  if (!class) { /* new class */
    class = TrClass_new(vm, name, TR_CLASS(Object));
    TrObject_const_set(vm, FRAME->class, name, class);
  }
  TrFrame_push(vm, b, class, class);
  TrVM_step(vm);
  TrFrame_pop(vm);
  return class;
}

static OBJ TrVM_interpret(VM, OBJ self, int argc, OBJ argv[]) {
  TrBlock *b = (TrBlock *)TR_CMETHOD(FRAME->method)->data;
  size_t i;
  if (b->argc != argc) tr_raise("Expected %lu arguments, got %d.\n", b->argc, argc);
  TrFrame_push(vm, b, self, TR_COBJECT(self)->class);
  /* transfer args */
  for (i = 0; i < argc; ++i) FRAME->locals[i] = argv[i];
  OBJ ret = TrVM_step(vm);
  TrFrame_pop(vm);
  return ret;
}

/* dispatch macros */
#define NEXT_OP        (++ip, e=*ip)
#ifdef TR_THREADED_DISPATCH
#define OPCODES        goto *labels[e.i]
#define END_OPCODES    
#define OP(name)       op_##name
#define DISPATCH       NEXT_OP; goto *labels[e.i]
#else
#define OPCODES        for(;;) { switch(e.i) {
#define END_OPCODES    default: printf("unknown opcode: %d\n", (int)e.i); }}
#define OP(name)       case TR_OP_##name
#define DISPATCH       NEXT_OP; break
#endif

/* register access macros */
#define A    (e.a)
#define B    (e.b)
#define C    (e.c)
#define R    regs
#define Bx   (unsigned short)(((B<<8)+C))
#define sBx  (short)(((B<<8)+C))
#define SITE (f->block->sites.a)

OBJ TrVM_step(VM) {
  TrFrame *f = FRAME;
  TrInst *ip = f->ip;
  register TrInst e = *ip;
  OBJ *k = f->block->k.a;
  char **strings = f->block->strings.a;
  register OBJ *regs = f->regs;
  OBJ *locals = f->locals;
  TrBlock **blocks = f->block->blocks.a;
  
#ifdef TR_THREADED_DISPATCH
  static void *labels[] = { TR_OP_LABELS };
#endif
  
  OPCODES;
    
    OP(BOING):      DISPATCH;
    
    /* register loading */
    OP(MOVE):       R[A] = R[B]; DISPATCH;
    OP(LOADK):      R[A] = k[Bx]; DISPATCH;
    OP(STRING):     R[A] = TrString_new2(vm, strings[Bx]); DISPATCH;
    OP(SELF):       R[A] = f->self; DISPATCH;
    OP(NIL):        R[A] = TR_NIL; DISPATCH;
    OP(BOOL):       R[A] = B+1; DISPATCH;
    OP(RETURN):     return R[A];
    
    /* variable and consts */
    OP(SETLOCAL):   locals[A] = R[B]; DISPATCH;
    OP(GETLOCAL):   R[A] = locals[B]; DISPATCH;
    OP(SETCONST):   TrObject_const_set(vm, f->self, k[Bx], R[A]); DISPATCH;
    OP(GETCONST):   R[A] = TrObject_const_get(vm, f->self, k[Bx]); DISPATCH;
    
    /* method calling */
    OP(LOOKUP):     R[A+1] = TrVM_lookup(vm, f, R[A], k[Bx], ip); DISPATCH;
    OP(CALL):       R[A] = TrVM_call(vm, f, R[A], R[A+1], B, &R[A+2]); DISPATCH;
    OP(CACHE):
      /* TODO how to expire cache? */
      assert(&SITE[C] && "Method cached but no CallSite found");
      if (SITE[C].class == TR_COBJECT(R[A])->class) {
        R[A+1] = SITE[C].method;
        ip += B;
      } else {
        /* TODO invalidate CallSite if too much miss. */
        SITE[C].miss++;
      }
      DISPATCH;
    
    /* definition */
    OP(DEF):
      TrClass_add_method(vm, f->class, k[Bx],
                         TrMethod_new(vm, (TrFunc *)TrVM_interpret, (OBJ)blocks[A], -1));
      DISPATCH;
    OP(CLASS): R[A] = TrVM_defclass(vm, f, k[Bx], blocks[A]); DISPATCH;
    
    /* jumps */
    OP(JMP):        ip += sBx; DISPATCH;
    OP(JMPIF):      if (TR_TEST(R[A])) ip += sBx; DISPATCH;
    OP(JMPUNLESS):  if (!TR_TEST(R[A])) ip += sBx; DISPATCH;
    
    /* optimizations */
    #define INLINE_FUNC(FNC) if (SITE[C].class == TR_COBJECT(R[A])->class) { FNC; ip += B; }
    OP(FIXNUM_ADD):
      INLINE_FUNC(R[A] = TrFixnum_new(vm, TR_FIX2INT(R[A]) + TR_FIX2INT(R[A+2])));
      DISPATCH;
    OP(FIXNUM_SUB):
      INLINE_FUNC(R[A] = TrFixnum_new(vm, TR_FIX2INT(R[A]) - TR_FIX2INT(R[A+2])));
      DISPATCH;
    OP(FIXNUM_LT):
      INLINE_FUNC(R[A] = TR_BOOL(TR_FIX2INT(R[A]) < TR_FIX2INT(R[A+2])));
      DISPATCH;
    
  END_OPCODES;
}

void TrVM_start(VM, TrBlock *b) {
  vm->self = TrObject_new(vm);
  vm->cf = 0;
  TrVM_run(vm, b, vm->self, TR_COBJECT(vm->self)->class);
}

OBJ TrVM_run(VM, TrBlock *b, OBJ self, OBJ class) {
  TrFrame_push(vm, b, self, class);
  OBJ ret = TrVM_step(vm);
  TrFrame_pop(vm);
  return ret;
}

TrVM *TrVM_new() {
  GC_INIT();

  TrVM *vm = TR_ALLOC(TrVM);
  vm->symbols = kh_init(str);
  vm->consts = kh_init(OBJ);
  
  /* bootstrap core classes */
  TrSymbol_init(vm);
  TrClass_init(vm);
  TrObject_init(vm);
  TrClass *symbolc = TR_CCLASS(TR_CLASS(Symbol));
  TrClass *classc = TR_CCLASS(TR_CLASS(Class));
  TrClass *objectc = TR_CCLASS(TR_CLASS(Object));
  symbolc->super = classc->super = (OBJ)objectc;
  symbolc->class = classc->class = objectc->class = (OBJ)classc;
  TR_COBJECT(tr_intern("Symbol"))->class = (OBJ)symbolc;
  
  TrPrimitive_init(vm);
  TrString_init(vm);
  TrFixnum_init(vm);
  TrArray_init(vm);
  
  return vm;
}

void TrVM_destroy(TrVM *vm) {
  kh_destroy(str, vm->symbols);
  GC_gcollect();
}
