#pragma once

#include <mruby.h>

namespace MRuby
{

struct ComponentFunctionSet
{
  using MrbFunction = mrb_value(*)(mrb_state*, entt::registry&, entt::entity, entt::registry::component_type);
  using MrbFunctionWithArg = mrb_value(*)(mrb_state*, entt::registry&, entt::entity, entt::registry::component_type, mrb_int, mrb_value*);

  MrbFunction has, get, remove;
  MrbFunctionWithArg set;
};
using ComponentFunctionMap = std::unordered_map< mrb_int, ComponentFunctionSet >;


template
<
  template<typename> typename ComponentInterface,
  typename... Components
>
void mrb_init_function_map(ComponentFunctionMap& map, entt::registry& registry)
{
  ((map[ registry.template type< Components >() ] = {
    ComponentInterface< Components >::has,
    ComponentInterface< Components >::get,
    ComponentInterface< Components >::remove,
    ComponentInterface< Components >::set
  }), ...);
}


template< typename Component >
struct DefaultComponentInterface
{
  static mrb_value get(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
  {
    if(registry.has<Component>(entity))
      return mrb_true_value();
    return mrb_false_value();
  }

  static mrb_value set(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type, mrb_int argc, mrb_value* args)
  {
    registry.assign_or_replace< Component >(entity);
    return mrb_true_value();
  }

  static mrb_value has(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
  {
    if(registry.has< Component >(entity))
      return mrb_true_value();
    return mrb_false_value();
  }

  static mrb_value remove(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
  {
    if(registry.has< Component >(entity))
    {
      registry.remove< Component >(entity);
      return mrb_true_value();
    }
    return mrb_false_value();
  }
};

template< typename Component >
struct ComponentInterface : DefaultComponentInterface< Component >
{
};

} // ::MRuby
