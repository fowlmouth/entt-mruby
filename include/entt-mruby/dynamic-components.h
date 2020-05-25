#pragma once

#include <unordered_map>

namespace MRuby
{

struct DynamicComponents
{
  std::unordered_map< mrb_int, mrb_value > components;
};

} // ::MRuby

MRUBY_COMPONENT_INTERFACE_BEGIN(MRuby::DynamicComponents)

  MRUBY_COMPONENT_GET
  {
    auto& dyn = registry.get_or_emplace< DynamicComponents >(entity);
    const auto iter = dyn.components.find(type);
    if(iter == dyn.components.cend())
      return mrb_nil_value();

    return iter->second;
  }

  MRUBY_COMPONENT_SET
  {
    auto& dyn = registry.get_or_emplace< DynamicComponents >(entity);
    mrb_gc_unregister(state, dyn.components[type]);
    if(argc == 0)
      dyn.components[type] = mrb_nil_value();
    else if(argc == 1)
      dyn.components[type] = argv[0];
    else
      dyn.components[type] = mrb_ary_new_from_values(state, argc, argv);
    mrb_gc_register(state, dyn.components[type]);
    return dyn.components[type];
  }

  MRUBY_COMPONENT_HAS
  {
    auto& dyn = registry.get_or_emplace< DynamicComponents >(entity);
    const auto iter = dyn.components.find(type);
    if(iter == dyn.components.cend())
      return mrb_false_value();

    return mrb_true_value();
  }

  MRUBY_COMPONENT_REMOVE
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

MRUBY_COMPONENT_INTERFACE_END

