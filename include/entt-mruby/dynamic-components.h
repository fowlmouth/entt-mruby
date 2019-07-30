#pragma once

#include <unordered_map>

namespace MRuby
{

struct DynamicComponents
{
  std::unordered_map< mrb_int, mrb_value > components;
};


template<>
struct ComponentInterface< DynamicComponents >
: DefaultComponentInterface< DynamicComponents >
{
  static mrb_value get(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
  {
    auto& dyn = registry.get_or_assign< DynamicComponents >(entity);
    const auto iter = dyn.components.find(type);
    if(iter == dyn.components.cend())
      return mrb_nil_value();

    return iter->second;
  }

  static mrb_value set(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type, mrb_int argc, mrb_value* arg)
  {
    auto& dyn = registry.get_or_assign< DynamicComponents >(entity);
    mrb_gc_unregister(state, dyn.components[type]);
    if(argc == 0)
      dyn.components[type] = mrb_nil_value();
    else if(argc == 1)
      dyn.components[type] = arg[0];
    else
      dyn.components[type] = mrb_ary_new_from_values(state, argc, arg);
    mrb_gc_register(state, dyn.components[type]);
    return dyn.components[type];
  }

  static mrb_value has(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
  {
    auto& dyn = registry.get_or_assign< DynamicComponents >(entity);
    const auto iter = dyn.components.find(type);
    if(iter == dyn.components.cend())
      return mrb_false_value();

    return mrb_true_value();
  }

  static mrb_value remove(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
  {
    if(auto dyn = registry.try_get< DynamicComponents >(entity))
    {
      auto& components = dyn->components;
      auto iter = components.find(type);
      if(iter == components.cend())
        return mrb_false_value();
      mrb_gc_unregister(state, iter->second);
      components.erase(iter);
      return mrb_true_value();
    }
    return mrb_false_value();
  }
};

} // ::MRuby
