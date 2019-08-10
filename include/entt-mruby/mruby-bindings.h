#pragma once

#include <mruby.h>
#include <mruby/data.h>
#include <mruby/compile.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/numeric.h>

#include "demangle-type-name.h"

#include <vector>

namespace MRuby
{
  struct Object
  {
    mrb_state* state;
    RObject* self;
    
    static Object new_(mrb_state* state, RClass* klass, int argc, const mrb_value* argv)
    {
      return Object{ state, mrb_obj_ptr( mrb_obj_new(state, klass, argc, argv) ) };
    }

    mrb_value value()
    {
      return mrb_obj_value(self);
    }

    auto define_singleton_method(const char* name, mrb_func_t func, mrb_aspec aspec)
    {
      return mrb_define_singleton_method(state, self, name, func, aspec);
    }
  };

  struct HashBuilder
  {
    mrb_state* state;
    mrb_value self;

    HashBuilder(mrb_state* state)
    : state(state)
    {
      self = mrb_hash_new(state);
    }


    HashBuilder& operator() (const char* symbol, mrb_value value)
    {
      mrb_hash_set(state, self,
        mrb_symbol_value(mrb_intern_cstr(state, symbol)),
        value);
      return *this;
    }
  };

  struct HashReader
  {
    mrb_state* state;
    mrb_value self;

    HashReader(mrb_state* state, mrb_value self)
    : state(state)
    {
      this->self = self;
    }

    template< typename T >
    HashReader& operator() (const char* symbol, T& value)
    {
      read_hash(*this, symbol, value);
      return *this;
    }

  };

  bool read_hash(HashReader& reader, const char* symbol, float& value)
  {
    mrb_value val = mrb_hash_get(
      reader.state,
      reader.self,
      mrb_symbol_value(mrb_intern_cstr(reader.state, symbol)));
    if(mrb_float_p(val))
    {
      value = mrb_to_flo(reader.state, val);
      return true;
    }
    return false;
  }

  bool read_hash(HashReader& reader, const char* symbol, mrb_int& value)
  {
    mrb_value val = mrb_hash_get(
      reader.state,
      reader.self,
      mrb_symbol_value(mrb_intern_cstr(reader.state, symbol)));
    if(mrb_fixnum_p(val))
    {
      value = mrb_fixnum(val);
      return true;
    }
    return false;
  }

  bool read_hash(HashReader& reader, const char* symbol, bool& value)
  {
    mrb_value val = mrb_hash_get(
      reader.state,
      reader.self,
      mrb_symbol_value(mrb_intern_cstr(reader.state, symbol)));
    value = mrb_bool(val);
    return true;
  }

  bool read_hash(HashReader& reader, const char* symbol, std::string& value)
  {
    mrb_value val = mrb_hash_get(
      reader.state,
      reader.self,
      mrb_symbol_value(mrb_intern_cstr(reader.state, symbol)));
    if(mrb_string_p(val))
    {
      value = mrb_string_value_cstr(reader.state, &val);
      return true;
    }
    return false;
  }

  bool read_hash(HashReader& reader, const char* symbol, std::vector< mrb_value >& value)
  {
    mrb_value val = mrb_hash_get(
      reader.state,
      reader.self,
      mrb_symbol_value(mrb_intern_cstr(reader.state, symbol)));
    if(mrb_array_p(val))
    {
      mrb_int len = ARY_LEN(mrb_ary_ptr(val));
      value.resize(len);
      for(int i = 0; i < len; ++i)
      {
        value[i] = mrb_ary_entry(val, i);
      }
      return true;
    }
    return false;
  }

  struct Module
  {
    mrb_state* state;
    RClass* self;

    static Module define(mrb_state* state, const char* name)
    {
      return Module{ state, mrb_define_module(state, name) };
    }

    Module& define_method(const char* name, mrb_func_t func, mrb_aspec aspec)
    {
      mrb_define_method(state, self, name, func, aspec);
      return *this;
    }

    Module& define_class_method(const char* name, mrb_func_t func, mrb_aspec aspec)
    {
      mrb_define_class_method(state, self, name, func, aspec);
      return *this;
    }

    operator RClass*()
    {
      return self;
    }
  };


  template< typename T >
  struct DefaultClassBinder
  {

    static void free(mrb_state* mrb, void* ptr)
    {
      if(ptr)
      {
        ((T*)ptr)->T::~T();
        mrb_free(mrb, ptr);
      }

    }

    static std::string type_name; // = ::type_name<T>();
    static mrb_data_type mrb_type;
    //  {
    //  type_name.c_str(), free
    // };

    static mrb_value init(mrb_state* mrb, mrb_value self)
    {
      T* ptr = (T*)DATA_PTR(self);
      if(ptr)
        mrb_free(mrb, ptr);

      // mrb_data_init(self, nullptr, &mrb_type);
      ptr = (T*)mrb_malloc(mrb, sizeof(T));
      if(!ptr)
      {
        // mrb->out_of_memory = true;
        mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err));
      }
      else
      {
        new(ptr) T();
        mrb_data_init(self, ptr, &mrb_type);
      }

      return self;
    }
    
  };
  template<typename T>
  std::string DefaultClassBinder<T>::type_name = MRuby::type_name<T>();
  template<typename T>
  mrb_data_type DefaultClassBinder<T>::mrb_type{
    DefaultClassBinder<T>::type_name.c_str(), DefaultClassBinder<T>::free
  };


  struct Class : Module
  {
    static Class define(mrb_state* state, const char* name, RClass* klass)
    {
      return Class{ state, mrb_define_class(state, name, klass) };
    }


    template
    <
      typename T,
      template<typename> typename Binder = DefaultClassBinder
    >
    static Class bind(
      mrb_state* state,
      const char* name,
      RClass* superclass,
      mrb_value(*init)(mrb_state*, mrb_value) = nullptr)
    {
      Class cls = define(state, name, superclass);
      MRB_SET_INSTANCE_TT(cls.self, MRB_TT_DATA);
      if(! init)
        init = Binder<T>::init;

      cls.define_method("initialize", init, MRB_ARGS_REQ(0));
      return cls;
    }

    mrb_value new_(int argc, mrb_value* argv)
    {
      return mrb_obj_new(state, self, argc, argv);
    }
  };
}
