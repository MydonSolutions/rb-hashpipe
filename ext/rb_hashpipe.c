/*
 * Document-module: Hashpipe
 *
 * Wraps functions provided by the Hashpipe library.
 */

/*
 * rb_hashpipe.c
 */

#include <hashpipe_status.h>

#include "ruby.h"

/*
 * If using old GUPPI names, define macros.
 */
#ifdef HAVE_TYPE_STRUCT_GUPPI_STATUS
#define hashpipe_status        guppi_status
#define hashpipe_status_attach guppi_status_attach
#define hashpipe_status_detach guppi_status_detach
#define hashpipe_status_lock   guppi_status_lock
#define hashpipe_status_unlock guppi_status_unlock
#endif // HAVE_TYPE_STRUCT_GUPPI_STATUS

#define Data_Get_HPStruct(self, s) \
  Data_Get_Struct(self, struct hashpipe_status, s);

#define Data_Get_HPStruct_Ensure_Detached(self, s) \
  Data_Get_HPStruct(self, s); \
  if(s->buf) rb_raise(rb_eRuntimeError, "already attached");

#define Data_Get_HPStruct_Ensure_Attached(self, s) \
  Data_Get_HPStruct(self, s); \
  if(!s->buf) rb_raise(rb_eRuntimeError, "not attached");

/*
 * Document-class: Hashpipe::Status
 *
 * A +Status+ object encapsulates a Hashpipe status buffer.
 */

static VALUE
rb_hps_alloc(VALUE klass)
{
  struct hashpipe_status * p;
  VALUE v;
  
  v = Data_Make_Struct(klass, struct hashpipe_status, 0, free, p);
  memset(p, 0, sizeof(struct hashpipe_status));
  return v;
}

// This is called by rb_thread_blocking_region withOUT GVL.
// Returns Qtrue on error, Qfalse on OK.
static VALUE
rb_hps_attach_blocking_func(void * s)
{
  int rc;

  rc = hashpipe_status_attach(
      ((struct hashpipe_status *)s)->instance_id,
      (struct hashpipe_status *)s);

  return rc ? Qtrue : Qfalse;
}

/*
 * call-seq: attach(instance_id) -> self
 *
 * Attaches to the status buffer of Hashpipe * instance given by +instance_id+
 * (Integer).  It is an error to call attach if already attached.
 */
VALUE rb_hps_attach(VALUE self, VALUE vid)
{
  int id;
  VALUE vrc;
  struct hashpipe_status tmp, *s;

  id = NUM2INT(vid);

  Data_Get_HPStruct_Ensure_Detached(self, s);

  // Ensure that instance_id field is set
  tmp.instance_id = id;

  vrc = rb_thread_blocking_region(
      rb_hps_attach_blocking_func, &tmp,
      RUBY_UBF_PROCESS, NULL);

  if(RTEST(vrc))
    rb_raise(rb_eRuntimeError, "could not attach to instance id %d", id);

  memcpy(s, &tmp, sizeof(struct hashpipe_status));

  return self;
}

/*
 * call-seq: Status.new(instance_id) -> Status
 *
 * Creates a Status object that is attached to the status buffer of Hashpipe
 * instance given by +instance_id+ (Integer).
 */
VALUE rb_hps_init(VALUE self, VALUE vid)
{
  return rb_hps_attach(self, vid);
}

/*
 * call-seq: detach -> self
 *
 * Detaches from the Hashpipe status buffer.  Future operations will fail until
 * attach is called.
 */
VALUE rb_hps_detach(VALUE self)
{
  int rc;
  struct hashpipe_status *s;

  Data_Get_HPStruct(self, s);

  if(s->buf) {
    rc = hashpipe_status_detach(s);

    if(rc != 0)
      rb_raise(rb_eRuntimeError, "could not detach");

    s->buf = 0;
  }

  return self;
}

/*
 * call-seq: attached? -> +true+ or +false+
 *
 * Returns true if +self+ is attached.
 */
VALUE rb_hps_attached_p(VALUE self)
{
  struct hashpipe_status *s;

  Data_Get_HPStruct(self, s);

  return s->buf ? Qtrue : Qfalse;
}

/*
 * call-seq: instance_id -> Integer (or nil)
 *
 * Returns instance ID if attached, otherwise +nil+.
 */
VALUE rb_hps_instance_id(VALUE self)
{
  struct hashpipe_status *s;

  Data_Get_HPStruct(self, s);

  return s->buf ? INT2NUM(s->instance_id) : Qnil;
}

/*
 * call-seq: unlock -> self
 *
 * Unlocks the status buffer relinguishing exclusive access.  You should always
 * unlock the status buffer after reading or modifying it.
 */
VALUE rb_hps_unlock(VALUE self)
{
  int rc;
  struct hashpipe_status *s;

  Data_Get_HPStruct_Ensure_Attached(self, s);

  rc = hashpipe_status_unlock(s);

  if(rc != 0)
    rb_raise(rb_eRuntimeError, "unlock error");

  return self;
}

// This is called by rb_thread_blocking_region withOUT GVL.
// Returns Qtrue on error, Qfalse on OK.
static VALUE
rb_hps_lock_blocking_func(void * s)
{
  int rc;
  rc = hashpipe_status_lock((struct hashpipe_status *)s);
  return rc ? Qtrue : Qfalse;
}

/*
 * call-seq: lock -> self
 *
 * Locks the status buffer for exclusive access.  You should always lock the
 * status buffer before reading or modifying it.
 */
VALUE rb_hps_lock(VALUE self)
{
  VALUE vrc;
  struct hashpipe_status *s;

  Data_Get_HPStruct_Ensure_Attached(self, s);

  vrc = rb_thread_blocking_region(
      rb_hps_lock_blocking_func, s,
      RUBY_UBF_PROCESS, NULL);

  if(RTEST(vrc))
    rb_raise(rb_eRuntimeError, "lock error");

  // If block given, yield self to the block, ensure unlock is called after
  // block finishes, and return block's return value.
  if(rb_block_given_p())
    return rb_ensure(rb_yield, self, rb_hps_unlock, self);
  else
    return self;
}

void Init_hashpipe()
{
  VALUE mHashpipe;
  VALUE cStatus;

  mHashpipe = rb_define_module("Hashpipe");
  cStatus = rb_define_class_under(mHashpipe, "Status", rb_cObject);

  rb_define_alloc_func(cStatus, rb_hps_alloc);
  rb_define_method(cStatus, "initialize", rb_hps_init, 1);
  rb_define_method(cStatus, "attach", rb_hps_attach, 1);
  rb_define_method(cStatus, "detach", rb_hps_detach, 0);
  rb_define_method(cStatus, "attached?", rb_hps_attached_p, 0);
  rb_define_method(cStatus, "instance_id", rb_hps_instance_id, 0);
  rb_define_method(cStatus, "unlock", rb_hps_unlock, 0);
  rb_define_method(cStatus, "lock", rb_hps_lock, 0);
}
