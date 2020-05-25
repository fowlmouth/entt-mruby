#pragma once

#include "mruby-bindings.h"
#include "dynamic-components.h"

#include <iterator>
#include <mruby/array.h>
#include <mruby/proc.h>
#include <mruby/error.h>
#include <iostream>

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
    registry.get id, registry.component_id(component)
  end

  def set component, *args
    registry.set id, registry.component_id(component), *args
  end

  def remove component
    registry.remove id, registry.component_id(component)
  end

  def has? component
    registry.has? id, registry.component_id(component)
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
    entities(*args) do |entity_id|
      yield entity entity_id
    end
  end
end
)MRUBY";

// Removes ::s and camel cases names (foo::bar::baz becomes FooBarBaz)
std::string cpp_type_name_to_mrb(const std::string& str)
{
    std::string result;
    auto capitalize = [](const std::string& s)->std::string
    {
        std::string res = s;
        if(std::islower(res[0]))
            res[0] = std::toupper(res[0]);
        return res;
    };
    std::size_t start = 0;
    std::size_t idx = str.find("::", start);
    while(idx != std::string::npos && start < str.size())
    {
        result.append(capitalize(str.substr(start, idx-start)));
        start = idx+2;
        idx = str.find("::", start);
    }
    if(start < str.size())
        result.append(capitalize(str.substr(start, str.size()-start)));
    
    return result;
}

template< typename Derived >
struct RegistryMixin
{
  // Derived must have a
  // - static const int called max_static_components
  // - member int called next_dynamic_component_id

  struct _mrb_component_type_info_t
  {
    entt::id_type index, id;
    bool is_dynamic;
    std::string name;
  };

  template< typename Component >
  static _mrb_component_type_info_t _mrb_component_type_info()
  {
    return {
      entt::type_index<Component>::value(),
      entt::type_info<Component>::id(),
      false,
      cpp_type_name_to_mrb(::MRuby::type_name<Component>())
    };
  }

  Derived& derived()
  {
    return *static_cast< Derived* >(this);
  }

  static Derived* mrb_value_to_registry(mrb_state* mrb, mrb_value value)
  {
    auto p = DATA_CHECK_GET_PTR(mrb, value, &::MRuby::DefaultClassBinder< MRubyRegistryPtr >::mrb_type, MRubyRegistryPtr);
    if(!p || !p->get())
      return nullptr;
    return p->get();
  }

  static void mrb_registry_free(mrb_state* mrb, void*)
  {
    return;
  }

  mrb_data_type mrb_registry_data_type{
    "Registry", mrb_registry_free
  };

  ComponentFunctionMap mrb_func_map;
  std::unordered_map< std::string, _mrb_component_type_info_t > mrb_dynamic_components;
  static std::unordered_map< entt::id_type, entt::id_type > _mrb_entt_type_index_to_id;

  // Create a new dynamic component, or return a component ID
  static mrb_value mrb_registry_new_component(
    mrb_state* mrb, mrb_value self)
  {
    Derived* registry = mrb_value_to_registry(mrb, self);
    if(!registry)
      return mrb_nil_value();

    const char* name;
    mrb_int size, id;
    if(mrb_get_args(mrb, "s", &name, &size) < 1)
      return mrb_nil_value();

    std::string strname(name, name+size);

    const auto iter = registry->mrb_dynamic_components.find(strname);
    if(iter != registry->mrb_dynamic_components.cend())
    {
      id = iter->second.index;
    }
    else
    {
      id = registry->next_dynamic_component_id++;
      registry->mrb_dynamic_components[ strname ] = {static_cast<entt::id_type>(id), entt::type_info<DynamicComponents>::id(), true, strname};
    }
    return mrb_fixnum_value(id);
  }

  // Return an array of component names
  static mrb_value mrb_registry_get_components(
    mrb_state* mrb, mrb_value self)
  {
    Derived* registry = mrb_value_to_registry(mrb, self);
    if(!registry)
      return mrb_nil_value();

    auto& mrb_dynamic_components = registry->mrb_dynamic_components;

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
    Derived* registry = mrb_value_to_registry(mrb, self);
    if(!registry)
      return mrb_nil_value();

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

    std::vector< entt::id_type > components, dynamic;

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
            components.push_back(entt::type_index< DynamicComponents >::value());
          }

          dynamic.push_back(type);
        }
      }
    }

    for(auto& component : components)
      component = _mrb_entt_type_index_to_id[component];

    auto view = registry->runtime_view(components.cbegin(), components.cend());

    if(dynamic.empty())
    {
      for(auto entity : view)
      {
        const auto id = std::underlying_type_t< entt::entity >(entity);
        mrb_yield(mrb, block, mrb_fixnum_value(id));
      }
    }
    else
    {
      for(const auto entity : view)
      {
        const auto id = std::underlying_type_t< entt::entity >(entity);

        const auto& others = registry->template get< DynamicComponents >(entity).components;
        const auto match = std::all_of(
          dynamic.cbegin(), dynamic.cend(),
          [&others](const auto type)
          {
            return others.find(type) != others.cend();
          }
        );
        if(match)
          mrb_yield(mrb, block, mrb_fixnum_value(id));
      }
    }

    return self;
  }
  

  static mrb_value mrb_registry_create(mrb_state* mrb, mrb_value self)
  {
    Derived* registry = mrb_value_to_registry(mrb, self);
    if(!registry)
      return mrb_nil_value();
    auto entity = registry->create();
    return mrb_fixnum_value(std::underlying_type_t< entt::entity >(entity));
  }


  using MrubyInvokeHandler = mrb_value(*)(mrb_state*, Derived*, ComponentFunctionSet&, mrb_int, mrb_int);

  static bool mrb_registry_unpack(
    mrb_state* mrb, mrb_value self,
    mrb_int& entity, mrb_int& type, mrb_value*& arg,
    mrb_int& arg_count,
    Derived*& registry,
    ComponentFunctionSet*& fn)
  {
    if(mrb_get_args(mrb, "ii*", &entity, &type, &arg, &arg_count) < 2)
      return false;

    registry = mrb_value_to_registry(mrb, self);
    if(!registry)
      return false;

    auto& iface = registry->mrb_func_map;
    const auto iter = iface.find(
      (type >= Derived::max_static_components)
        ? entt::type_index< DynamicComponents >::value()
        : type );
    if(iter == iface.cend())
      return false;

    fn = &iter->second;
    return true;
  }

  static mrb_value mrb_registry_valid(
    mrb_state* mrb, mrb_value self)
  {
    Derived* registry = mrb_value_to_registry(mrb, self);
    if(!registry)
      return mrb_nil_value();

    mrb_int entity;
    if(mrb_get_args(mrb, "i", &entity) != 1)
      return mrb_nil_value();

    return registry->valid(entt::entity(entity))
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
    std::string name = cpp_type_name_to_mrb< Component >();
    auto id = entt::type_index<Component>::value();
    mrb_define_const(state, ns, name.c_str(), mrb_fixnum_value(id));
    derived().mrb_dynamic_components[ name ] = _mrb_component_type_info<Component>();
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
      .define_method("entities", Derived::mrb_registry_entities, MRB_ARGS_ANY())
    ;

    ((mrb_init_component_name<Components>(state, registry_class)), ...);
    ((_mrb_entt_type_index_to_id[ entt::type_index<Components>::value() ] = entt::type_info<Components>::id()), ...);
    _mrb_entt_type_index_to_id[ entt::type_index<DynamicComponents>::value() ] = entt::type_info<DynamicComponents>::id();

    // Create a registry object
    auto registry_obj = registry_class.new_(0,nullptr);
    auto registry_data = (MRubyRegistryPtr*)DATA_PTR(registry_obj);
    registry_data->set(&derived());

    // Export it as "$registry" and "@registry"
    mrb_gv_set(state, mrb_intern_lit(state, "$registry"), registry_obj);
    mrb_iv_set(state, mrb_top_self(state), mrb_intern_lit(state, "@registry"), registry_obj);

    // Load the helper methods and Entity class
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


template< typename Derived >
std::unordered_map< entt::id_type, entt::id_type > RegistryMixin<Derived>::_mrb_entt_type_index_to_id;

} // ::MRuby
