#include "tinyrb.h"

static OBJ tr_lookup_method(VM, OBJ obj, OBJ name)
{
  OBJ met;
  
  /* lookup in metaclass methods */
  met = tr_hash_get(vm, TR_COBJ(obj)->metaclass->methods, name);
  if (met != TR_NIL)
    return met;
  
  /* lookup in class methods */
  met = tr_hash_get(vm, TR_COBJ(obj)->class->methods, name);
  if (met != TR_NIL)
    return met;
  
  /* decend class->super */
  tr_class *class = TR_COBJ(obj)->class->super;
  while (TR_NIL != (OBJ) class) {
    met = tr_hash_get(vm, class->methods, name);
    if (met != TR_NIL)
      return met;
    class = class->super;
  }
  
  /* method not found */
  /* TODO call method_missing */
  tr_raise(vm, "method not found: %s on %s", TR_STR(name), TR_STR(tr_object_inspect(vm, obj)));
  
  return TR_NIL;
}

OBJ tr_send(VM, OBJ obj, OBJ message, int argc, OBJ argv[])
{
  OBJ        met = tr_lookup_method(vm, obj, message);
  tr_method *m   = TR_CMETHOD(met);
  
  if (m->argc != argc)
    tr_raise(vm ,"wrong number of arguments: %d for %d", argc, m->argc);
      
  /* HACK better way to have variable num of args? */
  return m->func(vm, obj, argv[0], argv[1], argv[2], argv[3], argv[4],
                          argv[5], argv[6], argv[7], argv[8], argv[9]);
}

static OBJ tr_class_def(VM, tr_class *class, const char *name, OBJ (*func)(), int argc)
{
  tr_method *met = (tr_method *) tr_malloc(sizeof(tr_method));
  
  met->type = TR_METHOD;
  met->name = tr_intern(vm, name);
  met->func = func;
  met->argc = argc;
  
  tr_hash_set(vm, class->methods, met->name, (OBJ) met);
  
  return TR_NIL;
}

OBJ tr_def(VM, OBJ obj, const char *name, OBJ (*func)(), int argc)
{
  return tr_class_def(vm, TR_CCLASS(obj), name, func, argc);
}

OBJ tr_metadef(VM, OBJ obj, const char *name, OBJ (*func)(), int argc)
{
  return tr_class_def(vm, TR_COBJ(obj)->metaclass, name, func, argc);
}

OBJ tr_metaclass_new(VM)
{
  tr_class *c = (tr_class *) tr_malloc(sizeof(tr_class));
  
  c->type      = TR_CLASS;
  c->name      = tr_intern(vm, "");
  c->super     = (tr_class *) TR_NIL;
  c->ivars     = tr_hash_new(vm);
  c->methods   = tr_hash_new(vm);
  c->class     = (tr_class *) TR_NIL;
  c->metaclass = (tr_class *) TR_NIL;
  
  return (OBJ) c;
}

OBJ tr_class_new(VM, const char* name, OBJ super)
{
  tr_class *c = (tr_class *) tr_malloc(sizeof(tr_class));
  
  c->type      = TR_CLASS;
  c->name      = tr_intern(vm, name);
  c->super     = (tr_class *) super;
  c->ivars     = tr_hash_new(vm);
  c->methods   = tr_hash_new(vm);
  c->metaclass = (tr_class *) tr_metaclass_new(vm);
  if (strcmp(name, "Class") != 0) /* HACK */
    c->class   = (tr_class *) tr_const_get(vm, "Class");
  
  tr_const_set(vm, name, (OBJ) c);
  
  return (OBJ) c;
}

static OBJ tr_class_name(VM, OBJ self)
{
  return (OBJ) TR_CCLASS(self)->name;
}

void tr_class_init(VM)
{
  OBJ class = tr_class_new(vm, "Class", TR_NIL);
  
  tr_def(vm, class, "name", tr_class_name, 0);
}