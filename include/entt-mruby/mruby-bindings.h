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

  template< typename T >
  bool convert(mrb_state* state, mrb_value input, T& output)
  {
    return false;
  }


  template< typename T >
  bool read_hash(mrb_state* state, mrb_value hash, const char* symbol, T& output)
  {
    mrb_value value = mrb_hash_get(
      state,
      hash,
      mrb_symbol_value(mrb_intern_cstr(state, symbol)));
    return convert(state, value, output);
  }

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
    HashReader& operator() (const char* symbol, T& output)
    {
      read_hash(state, self, symbol, output);
      return *this;
    }

  };


  template<>
  bool convert<float>(mrb_state* state, mrb_value input, float& output)
  {
    if(mrb_float_p(input))
    {
      output = mrb_to_flo(state, input);
      return true;
    }
    return false;
  }

  template<>
  bool convert<mrb_int>(mrb_state* state, mrb_value input, mrb_int& output)
  {
    if(mrb_fixnum_p(input))
    {
      output = mrb_fixnum(input);
      return true;
    }
    return false;
  }

  template<>
  bool convert<bool>(mrb_state* state, mrb_value input, bool& output)
  {
    output = mrb_bool(input);
    return true;
  }

  template<>
  bool convert<std::string>(mrb_state* state, mrb_value input, std::string& output)
  {
    if(mrb_string_p(input))
    {
      output = mrb_string_value_cstr(state, &input);
      return true;
    }
    return false;
  }

  template<>
  bool convert< std::vector<mrb_value> >(mrb_state* state, mrb_value input, std::vector< mrb_value >& output)
  {
    if(mrb_array_p(input))
    {
      mrb_int len = ARY_LEN(mrb_ary_ptr(input));
      output.resize(len);
      for(int i = 0; i < len; ++i)
      {
        output[i] = mrb_ary_entry(input, i);
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
