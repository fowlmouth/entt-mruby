/*
  
  $ clang++ -std=c++1z -I ../entt/src -I ../json11 -DSM_MODULE_MRUBY mruby-test.cc ../json11/json11.cpp  -o mruby-test -lmruby

*/

#include <type_traits>
#include <cassert>
#include <map>
#include <string>


#include "entt-mruby/entt-mruby.h"
#include "entt-mruby/registry-mixin.h"


#include <mruby/hash.h>
#include <mruby/numeric.h>

#include <iostream>



struct Transform
{
  double x, y;
  double radians;
};

// struct Velocity
// {
//   double x, y;
// };




template<>
struct MRuby::ComponentInterface< Transform >
: MRuby::DefaultComponentInterface< Transform >
{
  static mrb_value get(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
  {
    auto& transform = registry.get_or_assign< Transform >(entity);

    mrb_value hash = mrb_hash_new(state);

    mrb_hash_set(state, hash,
      mrb_symbol_value(mrb_intern_lit(state, "x")),
      mrb_float_value(state, transform.x));
    mrb_hash_set(state, hash,
      mrb_symbol_value(mrb_intern_lit(state, "y")),
      mrb_float_value(state, transform.y));
    mrb_hash_set(state, hash,
      mrb_symbol_value(mrb_intern_lit(state, "radians")),
      mrb_float_value(state, transform.radians));

    return hash;
  }

  static mrb_value set(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type, mrb_int argc, mrb_value* arg)
  {
    if(!argc || ! mrb_hash_p(arg[0]))
      return mrb_nil_value();

    Transform new_transform;
    mrb_value x,y,radians;

    x = mrb_hash_get(state, arg[0], mrb_symbol_value(mrb_intern_lit(state, "x")));
    y = mrb_hash_get(state, arg[0], mrb_symbol_value(mrb_intern_lit(state, "y")));
    radians = mrb_hash_get(state, arg[0], mrb_symbol_value(mrb_intern_lit(state, "radians")));

    new_transform.x = mrb_to_flo(state, x);
    new_transform.y = mrb_to_flo(state, y);
    new_transform.radians = mrb_to_flo(state, radians);

    registry.assign_or_replace< Transform >(entity, new_transform);

    return arg[0];
  }
};


// template<>
// struct MRuby::ComponentInterface< Velocity >
// : MRuby::DefaultComponentInterface< Velocity >
// {
//   static mrb_value get(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type)
//   {
//     auto& velocity = registry.get_or_assign< Velocity >(entity);

//     mrb_value hash = mrb_hash_new(state);

//     mrb_hash_set(state, hash,
//       mrb_symbol_value(mrb_intern_lit(state, "x")),
//       mrb_float_value(state, velocity.x));
//     mrb_hash_set(state, hash,
//       mrb_symbol_value(mrb_intern_lit(state, "y")),
//       mrb_float_value(state, velocity.y));

//     return hash;
//   }

//   static mrb_value set(mrb_state* state, entt::registry& registry, entt::entity entity, entt::registry::component_type type, mrb_value arg)
//   {
//     if(! mrb_hash_p(arg))
//       return mrb_nil_value();
    
//     Velocity new_velocity;
//     mrb_value x,y;

//     x = mrb_hash_get(state, arg, mrb_symbol_value(mrb_intern_lit(state, "x")));
//     y = mrb_hash_get(state, arg, mrb_symbol_value(mrb_intern_lit(state, "y")));

//     new_velocity.x = mrb_to_flo(state, x);
//     new_velocity.y = mrb_to_flo(state, y);

//     registry.assign_or_replace< Velocity >(entity, new_velocity);

//     return arg;
//   }
// };

struct TestRegistry : entt::registry, MRuby::RegistryMixin< TestRegistry >
{
  mrb_state* state;
  // ComponentFunctionMap func_map;

  static const int max_static_components;
  int next_dynamic_component_id = max_static_components;

  TestRegistry()
  {
    state = mrb_open();
    this->mrb_init< Transform >(state); //, func_map);
  }

  mrb_value eval(const std::string& code)
  {
    return mrb_eval(state, code);
  }

  mrb_value load_file(const std::string& path)
  {
    // FILE* file = fopen(path.c_str(), "r");
    // mrb_value result = ::mrb_load_file(state, file);
    // fclose(file);
    mrb_value result = mrb_load_file(state, path);
    return result;
  }

};

const int TestRegistry::max_static_components = 32;


int main(int argc, const char** argv)
{
  std::string code;
  if(argc == 2)
    code = argv[1];


  TestRegistry registry;

  auto e1 = registry.create();
  auto& tr = registry.assign< Transform >(e1);
  tr.x = 2;
  tr.y = 2;
  tr.radians = M_PI;

  auto test = [&](const std::string& str)
  {
    std::cout << str << std::endl;
    mrb_value value = registry.eval(str);
    if(registry.state->exc)
    {
      mrb_print_error(registry.state);
      mrb_print_backtrace(registry.state);
      registry.state->exc = nullptr;
    }
    else
      mrb_p(registry.state, value);
    std::cout << std::endl;
  };

  test("$e = $registry.create_entity;\n"
    "$e.set('Transform', {x:0.0, y:0.0, radians:0.0})\n"
    "$e.set('Velocity', {x:10.0, y:10.0})\n"
  );
  test("10.times{\n"
    "  $registry.each_entity('Transform', 'Velocity'){ |e|\n"
    "    puts \"Entity: #{e.id}\"\n"
    "    transform = e.get('Transform')\n"
    "    velocity = e.get('Velocity')\n"
    "    transform[:x] += velocity[:x]\n"
    "    transform[:y] += velocity[:y]\n"
    "    e.set('Transform', transform)\n"
    "  }\n"
    "}\n"
  );
  test(
    "puts %Q{Velocity: #{$e.get('Velocity').inspect}}\n"
    "puts %Q{Transform: #{$e.get('Transform').inspect}}\n"
    "p $registry.all_components"
  );
  
  if(! code.empty())
    registry.eval(code);


  return 0;
}

