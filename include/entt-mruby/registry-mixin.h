#pragma once

#include "mruby-bindings.h"
#include "dynamic-components.h"

#include <iterator>
#include <mruby/array.h>
#include <mruby/proc.h>

namespace MRuby
{


template< typename T >
struct WeakPointer
{
  T* ptr;

  WeakPointer()
  : ptr(nullptr)
  {
  }

  T* operator->()
  {
    return ptr;
  }

  operator T*()
  {
    return ptr;
  }

  WeakPointer<T>& set(T* ptr)
  {
    this->ptr = ptr;
    return *this;
  }

  T* get()
  {
    return ptr;
  }
};

const char* mruby_api = R"MRUBY(
class Entity
  def initialize registry, id
    @registry = registry
    @id = id
  end

  attr_reader :registry, :id

  def valid?
    registry.valid? id
  end

  def get component
    component = registry.component(component) unless component.is_a?(Fixnum)
    registry.get id, component
  end

  def set component, *args
    component = registry.component(component) unless component.is_a?(Fixnum)
    registry.set id, component, *args
  end

  def remove component
    component = registry.component(component) unless component.is_a?(Fixnum)
    registry.remove id, component
  end

  def has? component
    component = registry.component(component) unless component.is_a?(Fixnum)
    registry.has? id, component
  end
end

class Registry
  
  def create_entity
    Entity.new self, create
  end

  def component_id id
    id === Fixnum ? id : component(id)
  end

  def get_component entity_id, component
    get entity_id, component_id(component)
  end

  def set_component entity_id, component, *args
    set entity_id, component_id(component), *args
  end

  def entity id
    Entity.new self, id
  end

  def each_entity *args, &block
    args = args.map {|id| component_id id }
    entities(*args, &block)
  end
end
)MRUBY";


template< typename Derived >
struct RegistryMixin
{
  // Derived must have a
  // - static const int called max_static_components
  // - member int called next_dynamic_component_id

  Derived& derived()
  {
    return *static_cast< Derived* >(this);
  }

  static void mrb_registry_free(mrb_state* mrb, void* p)
  {
    (void)0;
  }

  mrb_data_type mrb_registry_data_type{
    "Registry", mrb_registry_free
  };
  ComponentFunctionMap mrb_func_map;
  std::unordered_map< std::string, mrb_int > mrb_dynamic_components;

  // Create a new dynamic component, or return a component ID
  static mrb_value mrb_registry_new_component(
    mrb_state* mrb, mrb_value self)
  {
    auto p = (MRubyRegistryPtr*)DATA_PTR(self);
    if(!p || !p->get())
    {
      return mrb_nil_value();
    }

    Derived* ptr = p->get();

    const char* name;
    mrb_int size;
    if(mrb_get_args(mrb, "s", &name, &size) < 1)
      return mrb_nil_value();

    std::string strname(name, name+size);
    const auto iter = ptr->mrb_dynamic_components.find(strname);
    if(iter != ptr->mrb_dynamic_components.cend())
      return mrb_fixnum_value(iter->second);

    auto id = ptr->next_dynamic_component_id++;
    ptr->mrb_dynamic_components[ strname ] = id;
    return mrb_fixnum_value(id);
  }

  // Return an array of component names
  static mrb_value mrb_registry_get_components(
    mrb_state* mrb, mrb_value self)
  {
    auto p = (MRubyRegistryPtr*)DATA_PTR(self);
    if(!p || !p->get())
      return mrb_nil_value();

    Derived* ptr = p->get();
    auto& mrb_dynamic_components = ptr->mrb_dynamic_components;

    std::size_t num_components = mrb_dynamic_components.size();

    mrb_value array[num_components];
    auto iter = mrb_dynamic_components.begin();

    for(int i = 0; i < num_components; ++i)
    {
      const auto& name = (iter++)->first;
      array[i] = mrb_str_new(mrb, name.c_str(), name.size());
    }

    return mrb_ary_new_from_values(mrb, num_components, array);
  }

  static mrb_value mrb_registry_entities(
    mrb_state* mrb, mrb_value self)
  {
    auto p = (MRubyRegistryPtr*)DATA_PTR(self);
    if(!p || !p->get())
      return mrb_nil_value();

    Derived* ptr = p->get();
    const auto& max_static_components = Derived::max_static_components;

    mrb_value block = mrb_nil_value();
    mrb_value* args;
    mrb_int size;
    if(mrb_get_args(mrb, "*&", &args, &size, &block) == 0)
    {
      return mrb_nil_value();
    }

    RProc* proc = mrb_proc_ptr(block);
    if(!proc)
    {
      return mrb_nil_value();
    }

    std::vector< entt::registry::component_type > components, dynamic;

    for(int i = 0; i < size; ++i)
    {
      auto& arg = args[i];
      if(mrb_fixnum_p(arg))
      {
        mrb_int type = mrb_fixnum(arg);
        if(type < max_static_components)
        {
          components.push_back(type);
        }
        else
        {
          if(dynamic.empty())
          {
            components.push_back(ptr->template type< DynamicComponents >());
          }

          dynamic.push_back(type);
        }
      }
    }

    auto view = ptr->runtime_view(components.cbegin(), components.cend());
    // auto result = mrb_ary_new(mrb);

    if(dynamic.empty())
    {
      // No dynamic components, just add all entities to an array
      for(const auto entity : view)
      {
        const auto id = std::underlying_type_t< entt::entity >(entity);
        mrb_yield(mrb, block, mrb_fixnum_value(id));
        // mrb_ary_push(mrb, result, mrb_fixnum_value(id));
      }
    }
    else
    {
      for(const auto entity : view)
      {
        const auto id = std::underlying_type_t< entt::entity >(entity);

        const auto& others = ptr->template get< DynamicComponents >(entity).components;
        const auto match = std::all_of(
          dynamic.cbegin(), dynamic.cend(),
          [&others](const auto type)
          {
            return others.find(type) != others.cend();
          }
        );

        if(match)
          mrb_yield(mrb, block, mrb_fixnum_value(id));
          // mrb_ary_push(mrb, result, mrb_fixnum_value(id));
      }
    }

    return self; // result;
  }
  

  static mrb_value mrb_registry_create(mrb_state* mrb, mrb_value self)
  {
    MRubyRegistryPtr* ptr = (MRubyRegistryPtr*)DATA_PTR(self);
    if(!ptr)
      return mrb_nil_value();

    auto entity = ptr->get()->create();
    return mrb_fixnum_value(std::underlying_type_t< entt::entity >(entity));
  }


  using MrubyInvokeHandler = mrb_value(*)(mrb_state*, Derived*, ComponentFunctionSet&, mrb_int, mrb_int);

  static bool mrb_registry_unpack(
    mrb_state* mrb, mrb_value self,
    mrb_int& entity, mrb_int& type, mrb_value*& arg,
    mrb_int& arg_count,
    Derived*& ptr,
    ComponentFunctionSet*& fn)
  {
    if(mrb_get_args(mrb, "ii*", &entity, &type, &arg, &arg_count) < 2)
    {
      return false;
    }

    auto p = (MRubyRegistryPtr*)DATA_PTR(self);
    if(!p || !p->get())
    {
      return false;
    }

    ptr = p->get();

    auto& iface = ptr->mrb_func_map;
    const auto iter = iface.find(
      (type >= Derived::max_static_components) // ptr->max_static_components)
        ? ptr->template type< DynamicComponents >()
        : type );
    if(iter == iface.cend())
    {
      return false;
    }

    fn = &iter->second;
    return true;
  }

  static mrb_value mrb_registry_valid(
    mrb_state* mrb, mrb_value self)
  {
    auto p = (MRubyRegistryPtr*)DATA_PTR(self);
    if(!p || !p->get())
      return mrb_nil_value();
    
    Derived* ptr = p->get();

    mrb_int entity;
    if(mrb_get_args(mrb, "i", &entity) != 1)
      return mrb_nil_value();

    return ptr->valid(entt::entity(entity))
      ? mrb_true_value()
      : mrb_false_value();


  }

  static mrb_value mrb_registry_has(
    mrb_state* mrb, mrb_value self)
  {
    mrb_int entity, type;
    mrb_value* arg;
    mrb_int arg_count;
    Derived* ptr;
    ComponentFunctionSet* fn;
    if(mrb_registry_unpack(mrb, self, entity, type, arg, arg_count, ptr, fn))
      return fn->has(mrb, *ptr, (entt::entity)entity, type);
    return mrb_nil_value();
  }

  static mrb_value mrb_registry_get(
    mrb_state* mrb, mrb_value self)
  {
    mrb_int entity, type;
    mrb_value* arg;
    mrb_int arg_count;
    Derived* ptr;
    ComponentFunctionSet* fn;
    if(mrb_registry_unpack(mrb, self, entity, type, arg, arg_count, ptr, fn))
      return fn->get(mrb, *ptr, (entt::entity)entity, type);
    return mrb_nil_value();
  }

  static mrb_value mrb_registry_set(
    mrb_state* mrb, mrb_value self)
  {
    mrb_int entity, type;
    mrb_value* arg;
    mrb_int arg_count;
    Derived* ptr;
    ComponentFunctionSet* fn;
    if(mrb_registry_unpack(mrb, self, entity, type, arg, arg_count, ptr, fn))
      return fn->set(mrb, *ptr, (entt::entity)entity, type, arg_count, arg);
    return mrb_nil_value();
  }

  static mrb_value mrb_registry_remove(
    mrb_state* mrb, mrb_value self)
  {
    mrb_int entity, type;
    mrb_value* arg;
    mrb_int arg_count;
    Derived* ptr;
    ComponentFunctionSet* fn;
    if(mrb_registry_unpack(mrb, self, entity, type, arg, arg_count, ptr, fn))
      return fn->remove(mrb, *ptr, (entt::entity)entity, type);
    return mrb_nil_value();
  }


  using MRubyRegistryPtr = MRuby::WeakPointer< Derived >;

  template< typename Component >
  void mrb_init_component_name(mrb_state* state, RClass* ns)
  {
    std::string name = MRuby::type_name< Component >();
    auto id = derived().template type< Component >();
    mrb_define_const(state, ns, name.c_str(), mrb_fixnum_value(id));
    derived().mrb_dynamic_components[ name ] = id;
  }

  template< typename... Components >
  void mrb_init(mrb_state* state)
  {
    mrb_init_function_map<MRuby::ComponentInterface, MRuby::DynamicComponents, Components...>(
      derived().mrb_func_map, derived());

    // Set up an interface to access the registry from ruby
    auto registry_class = MRuby::Class::bind< MRubyRegistryPtr >(
      state, "Registry", state->object_class);

    registry_class
      .define_method("create", Derived::mrb_registry_create, MRB_ARGS_REQ(0))
      .define_method("get", Derived::mrb_registry_get, MRB_ARGS_REQ(2))
      .define_method("set", Derived::mrb_registry_set, MRB_ARGS_REQ(2))
      .define_method("remove", Derived::mrb_registry_remove, MRB_ARGS_REQ(2))
      .define_method("has?", Derived::mrb_registry_has, MRB_ARGS_REQ(2))
      .define_method("valid?", Derived::mrb_registry_valid, MRB_ARGS_REQ(1))
      .define_method("component", Derived::mrb_registry_new_component, MRB_ARGS_REQ(1))
      .define_method("all_components", Derived::mrb_registry_get_components, MRB_ARGS_REQ(0))
      .define_method("entities", Derived::mrb_registry_entities, MRB_ARGS_REQ(0))
    ;

    ((mrb_init_component_name<Components>(state, registry_class)), ...);

    // Create a registry object
    auto registry_obj = registry_class.new_(0,nullptr);
    auto registry_data = (MRubyRegistryPtr*)DATA_PTR(registry_obj);
    registry_data->set(&derived());

    // Export it as "$registry"
    mrb_gv_set(state, mrb_intern_lit(state, "$registry"), registry_obj);

    mrb_load_string(state, mruby_api);
  }

  void mrb_on_exception(mrb_state* state)
  {
    if(state->exc)
    {
      mrb_print_error(state);
      mrb_print_backtrace(state);
    }

  }

  mrb_value mrb_load_file(mrb_state* state, const std::string& path)
  {
    auto fp = fopen(path.c_str(), "r");
    mrb_value val = ::mrb_load_file(state, fp);
    fclose(fp);

    if(state->exc)
    {
        derived().mrb_on_exception(state);
        return mrb_nil_value();
    }

    return val;
  }

  mrb_value mrb_eval(mrb_state* state, const std::string& code)
  {
    mrb_value val = mrb_load_string(state, code.c_str());

    if(state->exc)
    {
      derived().mrb_on_exception(state);
      return mrb_nil_value();
    }

    return val;
  }
};

} // ::MRuby
