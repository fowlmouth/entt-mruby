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
  static mrb_value get(mrb_state* state, entt::registry& registry, entt::entity entity, entt::id_type type)
  {
    auto& transform = registry.get_or_emplace< Transform >(entity);

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

  static mrb_value set(mrb_state* state, entt::registry& registry, entt::entity entity, entt::id_type type, mrb_int argc, mrb_value* arg)
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

    registry.emplace_or_replace< Transform >(entity, new_transform);

    return arg[0];
  }
};


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
  {
    auto& tr = registry.emplace< Transform >(e1);
    tr.x = 2;
    tr.y = 2;
    tr.radians = M_PI;
  }

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

  test(R"MRUBY(
    $entity = $registry.create_entity
    $entity.set 'Transform', {x: 0.0, y: 0.0, radians: 0.0}
    $entity.set 'Velocity', {x: 10.0, y: 10.0}
  )MRUBY");

  test(R"MRUBY(
    10.times do
      #$registry.entities($registry.component_id('Transform'), $registry.component_id('Velocity')) do |e_id|
      $registry.each_entity('Transform', 'Velocity') do |e_id|
        e = $registry.entity e_id
        puts "Entity: #{e.id}"
        transform = e.get('Transform')
        velocity = e.get('Velocity')
        transform[:x] += velocity[:x]
        transform[:y] += velocity[:y]
        e.set('Transform', transform)
      end
    end
  )MRUBY");

  test(R"MRUBY(
    puts %Q{Velocity: #{ $entity.get('Velocity').inspect }}
    puts %Q{Transform: #{ $entity.get('Transform').inspect }}
    p $registry.all_components
  )MRUBY");
  
  if(! code.empty())
    registry.eval(code);


  return 0;
}

